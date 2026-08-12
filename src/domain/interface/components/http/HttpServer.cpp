#include "HttpServer.h"

#include "AccessLog.h"
#include "common/log/log.hpp"
#include "common/utils/queue/SegmentedMPSCQueue.hpp"
#include "Connection.h" // 注册表所有者 alive() 检查（仅弱指针无法识别已逻辑关闭的连接）
#include "Middleware.h"
#include "Reactor.h"
#include "Socket.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace web {

namespace {

constexpr size_t kMaxConnections = 256; // 同时连接上限（达上限拒绝新 accept）
constexpr int kPollTimeoutMs = 50;      // 单轮 select 有界超时（监听 + 连接就绪探测，M1）
// select 单轮 fd 集合上限（Windows FD_SETSIZE=64）。监听 fd 与连接 fd 合并进
// 同一个 select，给监听 fd 留一个槽位，故连接集合上限为 FD_SETSIZE - 1。准入
// 上限取 min(kMaxConnections, kMaxPolled)：达上限时新 accept 立即关闭——保证
// 每个被准入的连接都被 select 轮询，不会"准入但永不轮询 → 客户端永久挂起"（I-3）。
constexpr size_t kMaxPolled = FD_SETSIZE - 1;
// 准入上限（Windows min 宏会咬 std::min，故用三元）。
constexpr size_t kAdmitCap = (kMaxConnections < kMaxPolled) ? kMaxConnections : kMaxPolled;
constexpr int kSweepIntervalMs = 1000; // 超时清扫节拍（spec §4.2）

#ifdef _WIN32
using NativeFd = SOCKET;
#else
using NativeFd = int;
#endif
/// fd → select 原生类型（Windows SOCKET 是无符号句柄，需显式转换避免符号比较警告）。
inline NativeFd native_fd(int fd) {
    return static_cast<NativeFd>(fd);
}

/// 简单 {param} 路径匹配（与 Router::match_segments 同语义；Task 9 不需要参数值）。
bool match_path(const std::string& pattern, const std::string& path) {
    size_t pi = 0, si = 0;
    while (pi < pattern.size() && si < path.size()) {
        if (pattern[pi] == '{') {
            auto close = pattern.find('}', pi);
            if (close == std::string::npos)
                return false;
            if (close == pi + 1)
                return false;
            auto slash = path.find('/', si);
            size_t end = slash == std::string::npos ? path.size() : slash;
            if (end == si)
                return false;
            pi = close + 1;
            si = end;
        } else {
            if (pattern[pi] != path[si])
                return false;
            ++pi;
            ++si;
        }
    }
    return pi == pattern.size() && si == path.size();
}

} // namespace

HttpServer::HttpServer() = default;

struct HttpServer::Impl {
    struct Route {
        Method method;
        std::string pattern;
        Handler handler;
    };

    /// 一条注册：归属 Reactor 索引 + 连接所有者（weak）。所有者死亡（weak 失效）
    /// 的注册由 poller 投递/清扫时清理——fd 复用后旧注册不会残留，杜绝 select
    /// 对无主 fd 持续自旋投递（观测到 48k 次同 fd MISS 风暴）。
    struct Registration {
        size_t home;
        std::weak_ptr<Connection> owner;
    };

    TcpListener listener;
    std::atomic<bool> running{false};
    std::atomic<size_t> next_reactor{0}; // round-robin 归属游标
    size_t workers = 2;

    // poller 注册表（Poller 线程独占读写；loop 线程只经 poll_events 事件队列写，
    // 每轮 poll_once 开头 drain 独占应用 → 无锁，pmutex 已整体删除）
    std::unordered_map<int, Registration> home_of; // fd → {归属, 所有者}
    std::unordered_map<int, bool> want_write;      // fd → 是否监听写就绪
    // 延迟关闭队列（socket close 的唯一归属）：drain_poll_events 应用 Unregister
    // 事件时入队，poller 线程每轮开头清空。保证 select 期间快照中的 fd 永不提前
    // 关闭（无锁 select 的 EBADF 防线，见 poll_once 注释）。
    std::vector<int> close_queue;
    // 超时清扫节拍（仅 poller 线程读写，无需原子）
    std::chrono::steady_clock::time_point last_sweep{std::chrono::steady_clock::now()};

    /// loop 线程 → poller 的注册表写事件（锁自由队列；poller 独占应用 →
    /// pmutex 可整体删除：poller 是 home_of/want_write/close_queue 唯一
    /// 读者+写者）。
    struct PollEvent {
        enum class Op : uint8_t { Unregister, Interest } op;
        int fd;
        bool want_write = false;
    };
    /// 队列类型：loop 线程（多个 Reactor 各自推）→ poller 单消费者。
    /// SegmentedMPSCQueue 是 Multi-Producer Single-Consumer（多生产者单消费者，
    /// 见 common/utils/queue/SegmentedMPSCQueue.hpp 头部注释）——多 Reactor 推 +
    /// poller 单读正合适（设计文档 §3.2 指定复用）。try_push 恒真（无界），写
    /// 事件永不被丢弃——丢弃 Unregister 会让死 fd 残留 select 自旋风暴。
    SegmentedMPSCQueue<PollEvent> poll_events;

    std::vector<std::unique_ptr<Reactor>> reactors;

    std::vector<Route> routes;
    Handler fallback;
    std::vector<Middleware> middlewares;
    bool access_log = true;
};

bool HttpServer::start(const std::string& host, uint16_t port, size_t workers) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->workers = workers > 0 ? workers : 1;
    _impl->running.store(false, std::memory_order_release);
    if (!_impl->listener.listen(host, port))
        return false;
    LOG_INFO("http server start: %s:%u (workers=%zu)", host.c_str(), static_cast<unsigned>(_impl->listener.bound_port()),
             _impl->workers);
    return true;
}

uint16_t HttpServer::port() const noexcept {
    return _impl ? _impl->listener.bound_port() : 0;
}

void HttpServer::set_handler(Method method, std::string pattern, Handler h) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->routes.push_back(Impl::Route{method, std::move(pattern), std::move(h)});
}

void HttpServer::set_fallback(Handler h) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->fallback = std::move(h);
}

void HttpServer::use(Middleware m) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->middlewares.push_back(std::move(m));
}

void HttpServer::set_access_log(bool on) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->access_log = on;
}

void HttpServer::stop() noexcept {
    if (_impl)
        _impl->running.store(false, std::memory_order_release);
}

void HttpServer::run() {
    if (!_impl)
        return;
    auto& impl = *_impl;
    if (impl.running.exchange(true, std::memory_order_acq_rel))
        return; // 已在运行

    // 分发快照（set_handler/set_fallback 须在 run 前完成；run 期间只读）。
    Handler dispatch = [routes = impl.routes, fallback = impl.fallback](const HttpRequest& req) -> HttpResponse {
        for (const auto& r : routes)
            if (r.method == req.method && match_path(r.pattern, req.path))
                return r.handler(req);
        if (fallback)
            return fallback(req);
        return HttpResponse::json(404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}");
    };

    // middleware 链组装（run 前快照；逆序嵌套 → 注册序 = 外层→内层）。
    // Connection::process 拿到的仍是一个 Handler——传输路径零改动。
    for (auto it = impl.middlewares.rbegin(); it != impl.middlewares.rend(); ++it)
        dispatch = [m = *it, prev = std::move(dispatch)](const HttpRequest& req) { return m(req, prev); };

    // 默认访问日志（Combined 格式，INFO 级）：位于所有用户中间件之外——
    // 限流 429 等一切到达的请求都记录（nginx 惯例）。set_access_log(false)
    // 关闭；需要可信代理 XFF 策略时自装 make_access_logger(policy)。
    if (impl.access_log)
        dispatch = [m = make_access_logger(), prev = std::move(dispatch)](const HttpRequest& req) { return m(req, prev); };

    // 起 N 个 Reactor（EventLoop 消费线程）。
    impl.reactors.resize(impl.workers);
    for (size_t i = 0; i < impl.workers; ++i) {
        auto r = std::make_unique<Reactor>(dispatch);
        r->set_on_closed([this](int fd) { unregister_fd(fd); });
        r->set_on_interest([this](int fd, bool ww) { set_fd_interest(fd, ww); });
        r->start();
        impl.reactors[i] = std::move(r);
    }

    // Poller 线程：select 监听 + 连接；accept 后 round-robin 分片归属。
    std::thread poller([this] { poller_main(); });
    poller.join();
    LOG_INFO("http server stopped");

    // 优雅关闭：stop accept → 各 Reactor 关闭连接 → stop（join 消费线程）。
    for (auto& r : impl.reactors) {
        r->close_all();
        r->stop();
    }
    impl.reactors.clear();
    // poller 已退出（上面 poller.join() 返回）且各 loop 线程已 join：close_all
    // 推入的注销事件不再有 poller drain——此处兜底应用（Unregister → 延迟关闭
    // 入队），随后清空关闭队列（幂等：每 fd 至多入队一次，swap 即清）。此时
    // 队列单消费者约束成立：全部生产者（loop 线程）已 join。
    drain_poll_events();
    drain_close_queue();
}

void HttpServer::poller_main() {
    while (_impl->running.load(std::memory_order_acquire))
        poll_once();
}

void HttpServer::poll_once() {
    auto& impl = *_impl;

    // ① 注册表写事件 drain + 延迟关闭队列清空（socket close 唯一归属）：
    // 先于快照构建——本轮的 select 快照只包含仍打开（未注销）的 fd。
    // 事件由 loop 线程（unregister_fd / set_fd_interest）推入锁自由队列，
    // poller 独占应用：Unregister → 擦注册 + 关闭入队；Interest → 更新写兴趣。
    // drain 在每轮最前、快照在其后构建 → 顺序天然一致，快照内读无需锁。
    drain_poll_events();
    drain_close_queue();

    // ② 超时清扫（I-3）：每 ~1s 复查所有连接——独立于 select 结果，空闲服务器
    // （select 持续超时）也必须执行，否则慢读 5s 关闭 / keep-alive 30s 回收 /
    // SSE 心跳全部失效（spec §4.2 资源上限）。清扫任务投递到连接归属 Reactor，
    // 由 home loop 线程执行 Connection::sweep_check（零锁、fd 复用安全）。
    const auto sweep_now = std::chrono::steady_clock::now();
    if (sweep_now - impl.last_sweep >= std::chrono::milliseconds(kSweepIntervalMs)) {
        sweep_expired();
        impl.last_sweep = sweep_now;
    }

    // ③ 快照构建（无锁）：fd 列表 + 所有者身份 + 写兴趣。
    //
    // 注册表由 poller 独占读写（loop 线程只经 ① 的事件队列写）——快照构建、
    // sweep、drain 全部无锁。历史背景（2026-08-09，根因：锁饿死）：旧实现全程
    // 持 pmutex 跨 select——对端 FIN/未读数据使 select 每轮立即就绪（就绪 fd 未
    // 被消费则永不超时），poller 以微秒间隔连续重锁，Reactor 线程的 unregister_
    // fd/set_fd_interest 在 futex 唤醒竞速中连续落败 → 连接关闭/写兴趣更新被饿死
    // 数秒（WSL 观测 3s+，SSE 帧送达随之整体冻结）。该根因已随 pmutex 删除从
    // 结构上消失。快照持有 shared_ptr 身份 → 无锁 select → 投递前按"快照身份
    // == 注册表现况"双重校验。快照中的 fd 在本轮 select 期间绝不被关闭（关闭
    // 延迟到下一轮 ① drain），故无锁 select 不会对已关闭 fd 执行（EBADF 防线）。
    struct PollEntry {
        int fd;
        std::shared_ptr<Connection> owner; // 身份快照（fd 复用检测）
        size_t home;
        bool ww;
    };
    std::vector<PollEntry> entries;
    entries.reserve(kMaxPolled);
    int listener_fd = -1;
    {
        listener_fd = impl.listener.fd();
        for (auto it = impl.home_of.begin(); it != impl.home_of.end();) {
            const int fd = it->first;
            // 所有者已死（weak 失效或 _alive=false——逻辑关闭后对象可能仍被
            // hub/排队任务引用存活）：清理注册，不再轮询。否则 select 对残留
            // fd 持续自旋投递（已关闭连接 socket 上的未读 FIN/数据永报可读），
            // 队列堆积饿死其余连接（观测到 48k 次同 fd MISS 风暴）。
            auto sp = it->second.owner.lock();
            if (!sp || !sp->alive()) {
                impl.want_write.erase(fd);
                it = impl.home_of.erase(it);
                continue;
            }
            if (entries.size() >= kMaxPolled)
                break; // 连接集合上限 FD_SETSIZE-1：给监听 fd 留位（M1）
            auto ww = impl.want_write.find(fd);
            entries.push_back(PollEntry{fd, std::move(sp), it->second.home, ww != impl.want_write.end() && ww->second});
            ++it;
        }
    }
    if (listener_fd < 0 && entries.empty())
        return;

    // ④ 无锁 select（监听 fd 与全部连接 fd 合并进同一个 fd_set + 有界超时
    // kPollTimeoutMs=50，M1）：一轮 select 同时处理 accept 与连接就绪分发。
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    int maxfd = -1;
    if (listener_fd >= 0) {
        FD_SET(native_fd(listener_fd), &rfds);
        maxfd = listener_fd;
    }
    for (const auto& e : entries) {
        FD_SET(native_fd(e.fd), &rfds);
        if (e.ww)
            FD_SET(native_fd(e.fd), &wfds);
        if (e.fd > maxfd)
            maxfd = e.fd;
    }
    timeval tv{kPollTimeoutMs / 1000, (kPollTimeoutMs % 1000) * 1000};
    int n = ::select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
    if (n <= 0)
        return; // 超时 / EINTR → 下一轮重建（accept 与连接就绪均未发生）
    if (!impl.running.load(std::memory_order_acquire))
        return; // stop 发生在 select 阻塞期 → 直接退出

    // ⑤ 无锁：accept + 就绪投递（poller 独占注册表；快照身份校验杜绝 fd 复用
    // 串扰）。
    //
    // ⑤a 监听 fd 就绪 → accept 全部待连接 → round-robin 分片归属。
    if (listener_fd >= 0 && FD_ISSET(listener_fd, &rfds)) {
        // wait_ready(0) 非阻塞探测：无待连接即停，避免阻塞 accept 挂死 poll 循环。
        while (impl.listener.wait_ready(0) > 0) {
            if (!impl.running.load(std::memory_order_acquire))
                return;
            int c = impl.listener.accept();
            if (c < 0)
                break;
            set_nonblocking(c);
            size_t home = 0;
            // 准入上限 = min(kMaxConnections, kMaxPolled)：select 单轮 fd 集合
            // 被 FD_SETSIZE 封顶（监听 fd 占一席），准入超过该数目的连接会
            // "永不轮询 → 客户端永久挂起"。达上限拒绝新 accept（立即关闭，
            // 不响应任何字节）——保证每个被准入的连接都被 select 轮询（I-3
            // accept-cap 修复）。
            if (impl.home_of.size() >= kAdmitCap) {
                LOG_WARN("connection rejected: capacity cap %zu", kAdmitCap);
                sock_close(c); // 达上限：拒绝新连接（accept 后立即关闭，无响应）
                continue;
            }
            home = impl.next_reactor.fetch_add(1, std::memory_order_relaxed) % impl.workers;
            // 先建连接（conns 登记 + 驱动首发），后登记 poller 注册表——
            // 顺序不可颠倒：若先登记 home_of 而 add_connection 因同号 fd
            // 复用早退（conns 已有旧连接），会出现"注册在 poller 却无
            // Connection"的孤儿 → select 对无主 fd 自旋投递。
            auto conn = impl.reactors[home]->add_connection(c);
            if (!conn) {
                LOG_WARN("connection rejected: fd %d collides with live conn", c);
                sock_close(c);
                continue;
            }
            impl.home_of.emplace(c, Impl::Registration{home, conn});
            impl.want_write.emplace(c, false);
        }
    }

    // ⑤b 连接 fd 按归属投递。双重校验：注册表仍含该 fd（可能已被注销）且
    // 快照身份与现况一致（fd 可能已被 accept 复用给新连接——只投递旧身份）。
    for (const auto& e : entries) {
        bool rd = FD_ISSET(e.fd, &rfds) != 0;
        bool wr = FD_ISSET(e.fd, &wfds) != 0;
        if (!rd && !wr)
            continue;
        auto it = impl.home_of.find(e.fd);
        if (it == impl.home_of.end())
            continue; // 本轮 select 期间被注销（无锁 select 的正常竞态窗口）
        if (it->second.owner.lock() != e.owner)
            continue; // fd 复用：注册表现况是快照之后的新连接 → 不投递旧事件
        if (rd)
            impl.reactors[e.home]->on_readable(e.fd);
        else if (wr)
            impl.reactors[e.home]->on_writable(e.fd);
    }
}

void HttpServer::sweep_expired() {
    auto& impl = *_impl;
    // 注册表由 poller 独占读写（loop 线程只经事件队列写）——遍历无需锁；投递
    // 本身（loop.post）是锁无关操作。已注销/已关闭的 fd 在 ① drain 时已从注册
    // 表擦除，不会在这里被投递。
    for (auto it = impl.home_of.begin(); it != impl.home_of.end();) {
        auto sp = it->second.owner.lock();
        if (!sp || !sp->alive()) {
            impl.want_write.erase(it->first);
            it = impl.home_of.erase(it); // 所有者已死（weak 失效或已逻辑关闭）：清理注册（防自旋）
            continue;
        }
        impl.reactors[it->second.home]->check_timeout(it->first);
        ++it;
    }
}

void HttpServer::unregister_fd(int fd) {
    if (!_impl)
        return;
    // 推事件（锁自由队列，不持锁）：poller 每轮 ① drain 时独占应用——擦注册 +
    // 关闭入队。socket 不在 loop 线程关闭：select 的 fd 快照可能仍引用本 fd，
    // 若在 Reactor 线程立即关闭会命中"无锁 select 对已关闭 fd"竞态（EBADF）。
    // 延迟关闭保证快照中的 fd 在本轮 select 期间永不被关闭。关闭严格晚于
    // 注销（安全顺序），且由 poller 单线程执行、每 fd 至多一次（注销是
    // 身份校验过的幂等路径，见 Reactor::remove_connection）。事件迟到时以
    // poller 应用时的注册表现况为准（erase 幂等，fd 复用安全）。
    _impl->poll_events.try_push(Impl::PollEvent{Impl::PollEvent::Op::Unregister, fd});
}

/// 注册表写事件 drain（poller 每轮开头独占应用；关机兜底）。
void HttpServer::drain_poll_events() {
    if (!_impl)
        return;
    auto& impl = *_impl;
    // 独占应用（poller 单消费者；shutdown 兜底在全部生产者 join 后调用）。
    // 同 fd 事件按推入序应用（FIFO）：Interest 先于 Unregister 者先更新后擦除；
    // Unregister 之后到达的 Interest 因 fd 已擦除而被忽略——与注册表现况一致。
    Impl::PollEvent ev;
    while (impl.poll_events.try_pop(ev)) {
        if (ev.op == Impl::PollEvent::Op::Unregister) {
            impl.home_of.erase(ev.fd);
            impl.want_write.erase(ev.fd);
            impl.close_queue.push_back(ev.fd); // 关闭延迟到本轮 drain_close_queue
        } else {                               // Interest
            auto it = impl.want_write.find(ev.fd);
            if (it != impl.want_write.end())
                it->second = ev.want_write; // 未注册的 fd（已注销/未准入）：忽略
        }
    }
}

void HttpServer::drain_close_queue() {
    if (!_impl)
        return;
    auto& impl = *_impl;
    // poller 独占（或关机兜底时全线程已 join）：无锁 swap。
    std::vector<int> fds;
    fds.swap(impl.close_queue);
    for (int fd : fds)
        sock_close(fd);
}

void HttpServer::set_fd_interest(int fd, bool want_write) {
    if (!_impl)
        return;
    // 推事件（锁自由队列，不持锁）：poller 每轮 ① drain 时独占应用——写兴趣
    // 更新最迟下一轮 select 生效（≤ kPollTimeoutMs）。
    _impl->poll_events.try_push(Impl::PollEvent{Impl::PollEvent::Op::Interest, fd, want_write});
}

HttpServer::~HttpServer() {
    if (!_impl)
        return;
    _impl->running.store(false, std::memory_order_release);
    for (auto& r : _impl->reactors)
        if (r) {
            r->close_all();
            r->stop();
        }
    _impl->reactors.clear();
    drain_poll_events(); // run() 未走（start 后直接析构）时的兜底：应用残余注销事件
    drain_close_queue(); // 同上：清空延迟关闭队列
}

} // namespace web
