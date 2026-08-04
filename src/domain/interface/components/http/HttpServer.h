#pragma once
#include "HttpCommon.h"
#include "Reactor.h"
#include "Socket.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace web {

/// 多消费者 HTTP/1.1 服务器。
/// [Poller 线程] select 监听 + 全部连接 → 就绪事件按归属投递到对应 Reactor。
/// [N Reactor]   每连接终生归属一个 Reactor（其 EventLoop 消费线程处理）。
class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Bind+listen。port 0 → OS-assigned。workers 为 Reactor 分片数（默认 2）。
    bool start(const std::string& host, uint16_t port, size_t workers = 2);
    uint16_t port() const noexcept;
    void stop() noexcept;

    void set_handler(Method method, std::string pattern, Handler h);
    void set_fallback(Handler h);

    /// 阻塞运行；内部起 1 Poller 线程 + workers 个 Reactor 线程。stop() 后返回。
    void run();

private:
    void poller_main();         // Poller 线程主体
    void poll_once();           // 一轮 select：accept + 连接就绪投递
    void unregister_fd(int fd); // Reactor 关闭连接时调用（poller 注销）
    void set_fd_interest(int fd, bool want_write);

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace web
