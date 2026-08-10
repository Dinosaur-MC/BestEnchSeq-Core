#pragma once
#include "HttpCommon.h"
#include "Middleware.h"
#include <functional>
#include <memory>
#include <string>

namespace web {

/// 多消费者 HTTP/1.1 服务器。
/// [Poller 线程] select 监听 + 全部连接 → 就绪事件按归属投递到对应 Reactor。
/// [N Reactor]   每连接终生归属一个 Reactor（其 EventLoop 消费线程处理）。
///
/// 并发上限（I-3 accept-cap 修复）：poller 基于 ::select，单轮 fd 集合被
/// FD_SETSIZE（Windows 上 64）封顶。准入上限取 min(kMaxConnections=256,
/// FD_SETSIZE - 1)（监听 fd 占一个 select 槽位）——**每个被准入的连接都必然
/// 被 select 轮询**：达上限时新 accept
/// 立即关闭（不响应任何字节），不再出现"已准入但永不轮询 → 客户端永久挂起"。
/// 需要 >FD_SETSIZE 并发时改用 WSAPoll/poll（见 HttpServer.cpp 顶部注释）。
///
/// 资源上限（spec §4.2）：poller 每 ~1s 清扫一次连接超时——空闲 keep-alive
/// ~30s 关闭、部分请求慢读 ~5s 关闭、SSE 流空闲 ~15s 心跳 ping（写失败即关）。
/// 清扫事件投递到连接归属 Reactor，由 home loop 线程执行（零锁 + fd 复用安全）。
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

    /// 注册 middleware（run 前；与 set_handler 同约束：run 内快照、只读）。
    /// 按注册序嵌套，首个注册者最外层。默认访问日志（set_access_log(true)）
    /// 位于所有用户中间件之外（见 AccessLog.h）。
    void use(Middleware m);
    /// 默认访问日志开关（默认开启）：run() 内自动装配 make_access_logger() 于
    /// 所有用户中间件之外——限流 429 等一切到达的请求都记录（见 AccessLog.h）。
    /// 关闭后可用 use(make_access_logger(policy)) 自装自定义策略版本。
    void set_access_log(bool on);

    /// 阻塞运行；内部起 1 Poller 线程 + workers 个 Reactor 线程。stop() 后返回。
    void run();

private:
    void poller_main();         // Poller 线程主体
    void poll_once();           // 一轮 select：accept + 连接就绪投递 + 超时清扫
    void sweep_expired();       // 每 ~1s：把每个连接的超时复查投递给归属 Reactor
    void unregister_fd(int fd); // Reactor 关闭连接时调用（poller 注销 + 延迟关闭入队）
    void drain_close_queue();   // 关闭延迟队列（socket close 唯一归属；poller 每轮 + 关机）
    void set_fd_interest(int fd, bool want_write);

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace web
