// src/domain/interface/components/http/Connection.cpp
#include "Connection.h"
#include "Socket.h"
#include <memory>

namespace web {

Connection::Connection(int fd, std::string id) : _fd(fd), _id(std::move(id)) {}

Connection::~Connection() { close(); }

void Connection::close() {
    if (_alive) {
        _alive = false;
        sock_close(_fd);
        _fd = -1;
        // 关闭回调只触发一次：连接真正关闭后（_alive 已翻 false）通知订阅方退订。
        // 先移出再执行，避免回调重入 close() 时再次触发。
        auto cb = std::move(_on_close);
        _on_close = nullptr;
        if (cb) cb();
    }
}

void Connection::on_close(std::function<void()> cb) {
    _on_close = std::move(cb);
}

void Connection::set_frame_sink(std::function<void(std::string)> sink) {
    _frame_sink = std::move(sink);
}

void Connection::post_frame(std::string frame) {
    // 跨线程入口（solve worker 经 SseHub 回调）：只把帧交给 home-loop 帧汇，
    // 由 Reactor 把它 post 到 loop 线程再入缓冲 + 写出。未设置 sink（如单元测试
    // 直调/连接已卸下）→ 静默丢弃。
    if (_frame_sink)
        _frame_sink(std::move(frame));
}

void Connection::push_sse_frame(std::string frame) {
    if (!_alive) return;
    if (_stream) {
        _stream->raw(std::move(frame));
        flush_stream();
    }
}

void Connection::drain_out() {
    while (!_out.empty()) {
        int n = sock_send_nb(_fd, _out);
        if (n == -1) { close(); return; }    // 硬错误（含对端断开）→ 关闭连接
        if (n == 0) break;                   // would-block，等下次推进/WRITABLE
        _out.erase(0, static_cast<size_t>(n));
    }
}

void Connection::flush_stream() {
    if (!_alive || !_stream) return;
    auto now = std::chrono::steady_clock::now();
    // 心跳：流缓冲空闲超过 heartbeat_interval 且无新帧 → 注入 `: ping` 注释帧，
    // 供客户端/中间层保活，并让下一次写失败暴露对端断开。
    if (_stream->empty() && now - _last_write >= heartbeat_interval) {
        _stream->ping();
    }
    if (!_stream->empty()) {
        _out += _stream->drain();
        _last_write = std::chrono::steady_clock::now();
    }
    // 即使没有新帧也重试积压输出（被 would-block 打断的写靠后续推进补完）。
    drain_out();
}

bool Connection::set_stream(std::shared_ptr<SseStream> sse) {
    if (!_alive || _stream || !sse) return false;
    _stream = std::move(sse);
    _last_write = std::chrono::steady_clock::now();
    return true;
}

void Connection::push_sse_frame() {
    if (!_alive) return;
    if (_stream) flush_stream();
}

bool Connection::process(const Router& router) {
    if (!_alive) return false;

    // SSE 流模式：不再读/解析请求，只补完积压输出（写失败 → close 在 drain_out 内）。
    if (_stream) {
        bool had_out = !_out.empty();
        flush_stream();
        return had_out || !_out.empty();
    }

    bool progress = false;

    // 解析→按需增量读→再解析 循环。HttpParser 是增量无状态解析器：每次先试
    // 解析已缓冲字节；缺 body 或头部不完整时再从 socket 读一块、追加后重试，
    // 直到产出完整请求、出错、或暂时无更多数据。旧实现只在“头终结符尚未到达”
    // 时读，导致 body 落在后续 TCP 分段时永远 Incomplete 卡死。
    for (;;) {
        HttpRequest req;
        // 分发前把本连接挂到 req.stream：SSE events handler 用 StreamChannel 把
        // SseHub 帧投递回来。真实传输路径上连接由 shared_ptr 持有（enable_shared_
        // from_this 可解析）；单元测试里连接可能是栈对象 → shared_from_this 抛
        // bad_weak_ptr，此时留空（events handler 对空 channel 静默丢弃帧）。
        try {
            req.stream = shared_from_this();
        } catch (const std::bad_weak_ptr&) {
            req.stream = nullptr;
        }
        size_t consumed = 0;
        auto pr = _parser.parse(_in, consumed, req);
        if (pr == ParseResult::Complete) {
            _in.erase(0, consumed);
            auto resp = router(req);
            if (resp.is_stream) {
                // 升级为 SSE 流模式：写响应头，连接转入只写路径（push_sse_frame）。
                if (!_stream) {
                    _stream = std::make_shared<SseStream>(_id);
                    _last_write = std::chrono::steady_clock::now();
                }
                _out += resp.to_bytes();
                progress = true;
                break;                      // 流式连接脱离请求解析，不再读后续请求
            }
            _out += resp.to_bytes();
            progress = true;
            continue;                       // pipeline: may be another complete request buffered
        }
        if (pr == ParseResult::BadRequest) {
            _out += HttpResponse::bad_request("BAD_REQUEST", "malformed request").to_bytes();
            break;
        }
        // Incomplete → need more bytes from the socket. sock_recv_nb 的 0 既可能
        // 是 would-block 也可能是对端 FIN，先做就绪探测区分：不可读 → would-block，
        // 等下次推进；可读但 recv 仍返回 0 字节 → 真 FIN。
        if (wait_readable(_fd, 0) == 0) break;
        std::string chunk;
        int n = sock_recv_nb(_fd, chunk, 64 * 1024);
        if (n == -1) { close(); return progress; }
        if (n == 0) { _pending_eof = true; break; }   // 可读 + 0 字节 → 对端 FIN
        _in += chunk;
        progress = true;
    }

    drain_out();
    // EOF 且输出已全部写出 → 收尾关闭（对端不会再发数据）。
    if (_pending_eof && _out.empty()) close();
    return progress;
}

} // namespace web
