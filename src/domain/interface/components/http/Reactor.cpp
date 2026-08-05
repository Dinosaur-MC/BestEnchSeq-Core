#include "Reactor.h"
#include "Socket.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "common/utils/EventLoop.hpp"
#include <mutex>
#include "Connection.h"

namespace web {

struct Reactor::Impl {
    MPMCEventLoop<> loop;                                       // home loop 消费线程
    Handler handler;                                            // 请求分发（HttpServer 注入）
    OnClosed on_closed;                                         // fd 关闭 → poller 注销
    OnInterest on_interest;                                     // fd → wants_write → poller 写兴趣
    std::unordered_map<int, std::shared_ptr<Connection>> conns; // 仅经 mutex 访问
    std::mutex mutex;                                           // 保护 conns 跨线程增删
};

Reactor::Reactor(Handler h) : _impl(std::make_unique<Impl>()) {
    _impl->handler = std::move(h);
}

Reactor::~Reactor() {
    if (!_impl)
        return;
    close_all();        // 关闭仍存活的连接
    _impl->loop.stop(); // drain 剩余任务并 join 消费线程
}

void Reactor::set_on_closed(OnClosed fn) {
    _impl->on_closed = std::move(fn);
}
void Reactor::set_on_interest(OnInterest fn) {
    _impl->on_interest = std::move(fn);
}

void Reactor::start() {
    _impl->loop.start();
}
void Reactor::stop() {
    _impl->loop.stop();
}

void Reactor::add_connection(int fd) {
    {
        std::lock_guard lk(_impl->mutex);
        if (_impl->conns.count(fd) != 0)
            return;
        set_nonblocking(fd);
        auto conn = std::make_shared<Connection>(fd, std::to_string(fd));
        // StreamChannel 帧汇：post_frame → 本 Reactor → home loop 上 push_sse_frame。
        // 捕获 `this`（Reactor）安全：post 的任务在 loop 线程上于 Reactor 析构前
        // （loop.stop() 会 drain 剩余任务）全部执行完。
        // 捕获 weak_ptr<Connection> 而非 fd：连接关闭后 fd 可能被复用，fd 键会把旧帧
        // 串到新连接；weak_ptr 解析失败即静默丢弃。用 weak 也避免帧汇（存于连接自身）
        // 捕获自身 shared_ptr 形成环导致连接永不析构。
        conn->set_frame_sink([this, weak = std::weak_ptr<Connection>(conn)](std::string f) {
            if (auto sp = weak.lock())
                _impl->loop.post([sp, f = std::move(f)]() mutable {
                    sp->push_sse_frame(std::move(f));
                });
        });
        _impl->conns.emplace(fd, std::move(conn));
    }
    // 首推进：消费线程首次 process（请求可能已到达）
    _impl->loop.post([this, fd] { drive(fd); });
}

void Reactor::remove_connection(int fd) {
    // 先注销（poller 下一轮 select 重建时不再含该 fd），后销毁连接（关闭 socket）。
    if (_impl->on_closed)
        _impl->on_closed(fd);
    std::lock_guard lk(_impl->mutex);
    _impl->conns.erase(fd);
}

void Reactor::on_readable(int fd) {
    _impl->loop.post([this, fd] { drive(fd); });
}
void Reactor::on_writable(int fd) {
    _impl->loop.post([this, fd] { drive(fd); });
}

void Reactor::check_timeout(int fd) {
    // 捕获裸 `this` 与帧汇同安全（add_connection 注释）：loop.stop() drain 剩余
    // 任务，Reactor 析构前全部执行完。fd 复用安全：任务在 loop 线程复查
    // conns[fd]——连接已注销则空操作；fd 被新连接复用则新时间戳未到期。
    _impl->loop.post([this, fd] {
        std::shared_ptr<Connection> conn;
        {
            std::lock_guard lk(_impl->mutex);
            auto it = _impl->conns.find(fd);
            if (it == _impl->conns.end())
                return;
            conn = it->second;
        }
        switch (conn->sweep_check(std::chrono::steady_clock::now())) {
            case Connection::SweepAction::Heartbeat:
                // 心跳 flush：空闲 SSE 流注入 `: ping`；写失败 → drain_out 关闭
                // （对端断开检测）。驱动 connection 自身状态机，无锁需求。
                conn->push_sse_frame();
                break;
            case Connection::SweepAction::Close:
                remove_connection(fd);  // 注销（poller）→ 关闭 socket → on_close
                break;
            case Connection::SweepAction::None:
                break;
        }
    });
}

void Reactor::close_all() {
    std::vector<int> fds;
    {
        std::lock_guard lk(_impl->mutex);
        for (const auto& [fd, conn] : _impl->conns)
            fds.push_back(fd);
    }
    for (int fd : fds)
        if (_impl->on_closed)
            _impl->on_closed(fd);
    std::lock_guard lk(_impl->mutex);
    _impl->conns.clear(); // 关闭全部连接 socket
}

size_t Reactor::connection_count() const {
    std::lock_guard lk(_impl->mutex);
    return _impl->conns.size();
}

bool Reactor::empty() const {
    std::lock_guard lk(_impl->mutex);
    return _impl->conns.empty();
}

void Reactor::drive(int fd) {
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard lk(_impl->mutex);
        auto it = _impl->conns.find(fd);
        if (it == _impl->conns.end())
            return;
        conn = it->second;
    }
    // 锁外推进：process() 只触碰连接自身状态（解析/读/分发/写），而连接按零锁设计
    // 仅由本 loop 线程访问。把 shared_ptr 拷出后即可释放 Reactor 互斥量，避免持锁
    // 跨越整个 parse+dispatch+write，阻塞 poller 线程的 add_connection/remove_connection。
    conn->process(_impl->handler);
    if (!conn->alive()) {
        remove_connection(fd); // EOF/错误 → 注销并关闭
    } else if (_impl->on_interest) {
        _impl->on_interest(fd, conn->wants_write()); // 同步写兴趣给 poller
    }
}

} // namespace web
