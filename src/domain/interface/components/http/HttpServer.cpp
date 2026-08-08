#include "HttpServer.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include "Socket.h"
#include "Reactor.h"
#include "Connection.h" // 注册表所有者 alive() 检查（仅弱指针无法识别已逻辑关闭的连接）
#include "common/log/log.hpp"

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
constexpr int kSweepIntervalMs = 1000;  // 超时清扫节拍（spec §4.2）

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

    // poller 注册表（Poller 线程 + Reactor 线程经 pmutex 共享）
    std::mutex pmutex;
    std::unordered_map<int, Registration> home_of; // fd → {归属, 所有者}
    std::unordered_map<int, bool> want_write;      // fd → 是否监听写就绪
    // 超时清扫节拍（仅 poller 线程读写，无需原子）
    std::chrono::steady_clock::time_point last_sweep{std::chrono::steady_clock::now()};

    std::vector<std::unique_ptr<Reactor>> reactors;

    std::vector<Route> routes;
    Handler fallback;
};

bool HttpServer::start(const std::string& host, uint16_t port, size_t workers) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->workers = workers > 0 ? workers : 1;
    _impl->running.store(false, std::memory_order_release);
    if (!_impl->listener.listen(host, port))
        return false;
    LOG_INFO("http server start: %s:%u (workers=%zu)", host.c_str(),
             static_cast<unsigned>(_impl->listener.bound_port()), _impl->workers);
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
}

void HttpServer::poller_main() {
    while (_impl->running.load(std::memory_order_acquire))
        poll_once();
}

void HttpServer::poll_once() {
    auto& impl = *_impl;

    // ① 超时清扫（I-3）：每 ~1s 复查所有连接——独立于 select 结果，空闲服务器
    // （select 持续超时）也必须执行，否则慢读 5s 关闭 / keep-alive 30s 回收 /
    // SSE 心跳全部失效（spec §4.2 资源上限）。清扫任务投递到连接归属 Reactor，
    // 由 home loop 线程执行 Connection::sweep_check（零锁、fd 复用安全）。
    const auto sweep_now = std::chrono::steady_clock::now();
    if (sweep_now - impl.last_sweep >= std::chrono::milliseconds(kSweepIntervalMs)) {
        sweep_expired();
        impl.last_sweep = sweep_now;
    }

    // ② 监听 fd 与全部连接 fd 合并进同一个 select（单一 fd_set + 有界超时
    // kPollTimeoutMs=50）：一轮 select 同时处理 accept 与连接就绪分发——空闲时
    // 已建立连接上的请求发现延迟从 ≤100ms 降到 ≤50ms（M1）。全程持 pmutex 跨
    // select：fd 集合构建、select 系统调用、accept 准入、就绪投递与 Reactor 的
    // unregister（on_closed → unregister_fd）串行化，杜绝“集合已建好、socket 已
    // 被关闭、select 仍对已关闭 fd 执行”的竞态。持锁期间 Reactor 线程的
    // unregister_fd/set_fd_interest 只会短暂阻塞等待，而非死锁（锁序：poller 持
    // pmutex 时不取 Reactor mutex，故无环）。
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    std::vector<int> fds; // 连接 fd 快照（不含监听 fd）
    int maxfd = -1;
    const int listener_fd = impl.listener.fd();
    const bool have_listener = listener_fd >= 0;
    {
        std::lock_guard lk(impl.pmutex);
        if (have_listener) {
            FD_SET(native_fd(listener_fd), &rfds);
            maxfd = listener_fd;
        }
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
            if (fds.size() >= kMaxPolled)
                break; // 连接集合上限 FD_SETSIZE-1：给监听 fd 留位（M1）
            fds.push_back(fd);
            FD_SET(native_fd(fd), &rfds);
            auto ww = impl.want_write.find(fd);
            if (ww != impl.want_write.end() && ww->second)
                FD_SET(native_fd(fd), &wfds);
            if (fd > maxfd)
                maxfd = fd;
            ++it;
        }
        if (maxfd < 0)
            return;

        timeval tv{kPollTimeoutMs / 1000, (kPollTimeoutMs % 1000) * 1000};
        int n = ::select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
        if (n <= 0)
            return; // 超时 / EINTR → 下一轮重建（accept 与连接就绪均未发生）
        if (!impl.running.load(std::memory_order_acquire))
            return; // stop 发生在 select 阻塞期 → 直接退出

        // ③ 监听 fd 就绪 → accept 全部待连接 → round-robin 分片归属。
        if (have_listener && FD_ISSET(listener_fd, &rfds)) {
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

        // ④ 连接 fd 按归属投递（与 select 同一 pmutex 作用域：就绪状态与注册表
        // 一致，杜绝投递到 select 返回后刚被注销/关闭的 fd）。
        for (int fd : fds) {
            auto it = impl.home_of.find(fd);
            if (it == impl.home_of.end())
                continue; // 防御：持锁跨 select，正常不应被注销
            if (!it->second.owner.lock())
                continue; // 所有者已死（本轮的防御性复查）
            size_t home = it->second.home;
            bool rd = FD_ISSET(fd, &rfds) != 0;
            bool wr = FD_ISSET(fd, &wfds) != 0;
            if (rd)
                impl.reactors[home]->on_readable(fd);
            else if (wr)
                impl.reactors[home]->on_writable(fd);
        }
    }
}

void HttpServer::sweep_expired() {
    auto& impl = *_impl;
    // 持 pmutex 遍历注册表：与 Reactor 的 unregister_fd（on_closed → 注销）串行化，
    // 保证不对已注销/已关闭的 fd 投递。投递本身（loop.post）是锁无关操作，不构成
    // 锁序环（poller: pmutex → 队列；Reactor: 队列消费 → pmutex，队列锁不跨任务持有）。
    std::lock_guard lk(impl.pmutex);
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
    std::lock_guard lk(_impl->pmutex);
    _impl->home_of.erase(fd);
    _impl->want_write.erase(fd);
    // 不在此关 socket：注册表注销必须无条件执行（防 select 对已拆除连接的 fd
    // 持续自旋投递 → 队列堆积饿死其余连接）；socket 关闭由 Reactor 按对象身份
    // 决定（迟到拆除不得误杀 fd 复用后的新连接），且严格晚于本注销（安全顺序）。
}

void HttpServer::set_fd_interest(int fd, bool want_write) {
    if (!_impl)
        return;
    std::lock_guard lk(_impl->pmutex);
    auto it = _impl->want_write.find(fd);
    if (it != _impl->want_write.end())
        it->second = want_write;
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
}

} // namespace web
