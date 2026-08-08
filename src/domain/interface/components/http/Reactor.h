#pragma once
#include "HttpCommon.h"
#include <functional>
#include <memory>

namespace web {

class Connection;  // 仅指针参数（remove_connection），定义在 Connection.h

/// 单个 home loop：一个 EventLoop 消费线程 + 该 loop 专属的连接表。
/// 连接事件（读/写/关闭/SSE帧）只在本 loop 线程处理 → 连接零锁。
class Reactor {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    /// fd 关闭回调（HttpServer 注入：通知 poller 注销，保证 select 不含已关闭 fd）。
    using OnClosed = std::function<void(int)>;
    /// fd 兴趣回调（HttpServer 注入：同步 wants_write 给 poller 的写就绪监听）。
    using OnInterest = std::function<void(int, bool)>;

    explicit Reactor(Handler h);
    ~Reactor();
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    void set_on_closed(OnClosed fn);
    void set_on_interest(OnInterest fn);

    void start();                // 启动消费线程
    void stop();                 // 停止并 join（优雅 drain）
    /// 从 poller 线程调用：登记连接并返回其 shared_ptr（供 poller 注册表携带
    /// 所有者）；fd 撞号（conns 已有同号连接）返回 nullptr，调用方应关闭 socket。
    std::shared_ptr<Connection> add_connection(int fd);
    /// 按对象身份拆除连接（fd 复用安全：conns[fd] 非本对象则 no-op）。
    void remove_connection(int fd, const std::shared_ptr<Connection>& conn);
    void on_readable(int fd); // poller 投递
    void on_writable(int fd);
    /// 超时清扫（I-3）：poller 线程每 ~1s 对每个 fd 调用。post 到 home loop，
    /// 在 loop 线程复查连接超时状态并按需心跳/关闭（连接零锁设计；fd 复用
    /// 安全——复查时 conns[fd] 已是新连接则新鲜时间戳不会误伤）。
    void check_timeout(int fd);
    void close_all();                           // 优雅关闭：清空连接

    size_t connection_count() const;
    bool empty() const;

private:
    void drive(int fd); // loop 线程推进一条连接

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace web
