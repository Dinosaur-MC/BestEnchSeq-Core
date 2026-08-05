#include "HttpServer.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <vector>
#include <atomic>
#include "Socket.h"
#include "Reactor.h"

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
constexpr int kAcceptPollMs = 100;      // 监听就绪探测上限（同时作 stop 节拍）
constexpr int kConnPollMs = 0;          // 连接就绪探测：非阻塞（0 超时）
// select 单轮 fd 集合上限（Windows FD_SETSIZE=64）。准入上限取
// min(kMaxConnections, kMaxPolled)：达上限时新 accept 立即关闭——保证每个被
// 准入的连接都被 select 轮询，不会"准入但永不轮询 → 客户端永久挂起"（I-3）。
constexpr size_t kMaxPolled = FD_SETSIZE;
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

    TcpListener listener;
    std::atomic<bool> running{false};
    std::atomic<size_t> next_reactor{0}; // round-robin 归属游标
    size_t workers = 2;

    // poller 注册表（Poller 线程 + Reactor 线程经 pmutex 共享）
    std::mutex pmutex;
    std::unordered_map<int, size_t> home_of;  // fd → 归属 Reactor 索引
    std::unordered_map<int, bool> want_write; // fd → 是否监听写就绪
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
    return _impl->listener.listen(host, port);
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

    // ① 监听 fd 就绪（阻塞 ≤ kAcceptPollMs；返回后立即检查 stop）。
    int lr = impl.listener.wait_ready(kAcceptPollMs);
    if (!impl.running.load(std::memory_order_acquire))
        return;

    // ② accept 全部待连接 → round-robin 分片归属。
    if (lr > 0) {
        // wait_ready(0) 非阻塞探测：无待连接即停，避免阻塞 accept 挂死 poll 循环。
        while (impl.listener.wait_ready(0) > 0) {
            if (!impl.running.load(std::memory_order_acquire))
                return;
            int c = impl.listener.accept();
            if (c < 0)
                break;
            set_nonblocking(c);
            size_t home = 0;
            bool ok = false;
            {
                std::lock_guard lk(impl.pmutex);
                // 准入上限 = min(kMaxConnections, kMaxPolled)：select 单轮 fd 集合
                // 被 FD_SETSIZE 封顶，准入超过该数目的连接会"永不轮询 → 客户端
                // 永久挂起"。达上限拒绝新 accept（立即关闭，不响应任何字节）——
                // 保证每个被准入的连接都被 select 轮询（I-3 accept-cap 修复）。
                if (impl.home_of.size() < kAdmitCap) {
                    home = impl.next_reactor.fetch_add(1, std::memory_order_relaxed) % impl.workers;
                    impl.home_of.emplace(c, home);
                    impl.want_write.emplace(c, false);
                    ok = true;
                }
            }
            if (!ok) {
                sock_close(c); // 达上限：拒绝新连接（accept 后立即关闭，无响应）
                continue;
            }
            impl.reactors[home]->add_connection(c);
        }
    }

    // ③ 超时清扫（I-3）：每 ~1s 把每个连接的超时复查投递给其归属 Reactor。
    // home loop 线程执行 Connection::sweep_check（零锁、fd 复用安全）并关闭到期
    // 连接/注入 SSE 心跳——慢客户端、空闲 keep-alive 与半开 SSE 都有界。
    const auto sweep_now = std::chrono::steady_clock::now();
    if (sweep_now - impl.last_sweep >= std::chrono::milliseconds(kSweepIntervalMs)) {
        sweep_expired();
        impl.last_sweep = sweep_now;
    }

    // ③ 连接就绪探测（非阻塞 select）→ 按归属投递。
    // 全程持 pmutex 跨 select：fd 集合构建、select 系统调用、就绪投递三者与
    // Reactor::remove_connection 的 unregister+close 串行化（关闭走 on_closed→
    // unregister_fd 取同一把锁），杜绝“集合已建好、socket 已被关闭、select 仍对
    // 已关闭 fd 执行”的竞态。kConnPollMs=0 → select 非阻塞即时返回，锁窗口极短；
    // 持锁期间 Reactor 线程的 unregister_fd 只会短暂阻塞等待，而非死锁（锁序：
    // poller 持 pmutex 时不取 Reactor mutex，故无环）。
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    std::vector<int> fds;
    int maxfd = -1;
    {
        std::lock_guard lk(impl.pmutex);
        for (const auto& [fd, home] : impl.home_of) {
            if (fds.size() >= FD_SETSIZE)
                break; // select 单轮 fd 数上限保护（Windows FD_SETSIZE=64）
            fds.push_back(fd);
            FD_SET(native_fd(fd), &rfds);
            auto it = impl.want_write.find(fd);
            if (it != impl.want_write.end() && it->second)
                FD_SET(native_fd(fd), &wfds);
            if (fd > maxfd)
                maxfd = fd;
        }
        if (fds.empty())
            return;

        timeval tv{kConnPollMs / 1000, (kConnPollMs % 1000) * 1000};
        int n = ::select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
        if (n <= 0)
            return; // 超时 / EINTR → 下一轮重建

        for (int fd : fds) {
            size_t home;
            auto it = impl.home_of.find(fd);
            if (it == impl.home_of.end())
                continue; // 防御：持锁跨 select，正常不应被注销
            home = it->second;
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
    for (const auto& [fd, home] : impl.home_of)
        impl.reactors[home]->check_timeout(fd);
}

void HttpServer::unregister_fd(int fd) {
    if (!_impl)
        return;
    std::lock_guard lk(_impl->pmutex);
    _impl->home_of.erase(fd);
    _impl->want_write.erase(fd);
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
