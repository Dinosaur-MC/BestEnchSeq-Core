// src/domain/interface/components/http/Connection.cpp
#include "Connection.h"
#include "common/log/log.hpp"
#include "Socket.h"
#include <memory>

namespace web {

namespace {
// 日志消毒：请求路径是客户端可控字节，可能含 \r/\n/控制字符 → 日志注入面，
// 记日志前把 < 0x20 的字符替换为 '_'。
std::string sanitize_for_log(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20)
            c = '_';
    return out;
}
} // namespace

Connection::Connection(int fd, std::string id) : _fd(fd), _id(std::move(id)) {
    touch();                      // 活动基准：从 accept/构造时刻起计时（空闲 keep-alive 30s 到期）
    _remote = sock_peer_addr(fd); // 对端 IP：限流 key 与访问日志 IP 字段
}

Connection::~Connection() {
    close();
    // 仅对未走注销路径的连接（单元测试直调/从未注册）在此关闭 socket。
    // 服务器路径经 remove_connection/close_all 注销后 detach_fd() 置 _fd=-1，
    // 关闭权移交 HttpServer 的延迟关闭队列（poller 线程 drain）——这里不关闭，
    // 也不会二次关闭。
    if (_fd >= 0) {
        sock_close(_fd);
        _fd = -1;
    }
}

void Connection::close() {
    if (_alive) {
        LOG_DEBUG("conn %s closed", _id.c_str());
        _alive = false;
        // 关闭回调只触发一次：连接真正关闭后（_alive 已翻 false）通知订阅方退订。
        // 先移出再执行，避免回调重入 close() 时再次触发。
        auto cb = std::move(_on_close);
        _on_close = nullptr;
        if (cb)
            cb();
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
    if (!_alive)
        return;
    if (_stream) {
        _stream->raw(std::move(frame));
        flush_stream();
    }
}

void Connection::drain_out() {
    // 局部累计已写偏移，循环结束后一次 erase：避免每轮 send 后 `erase(0, n)` 的
    // O(n) memmove——慢读客户端 + 大响应时反复部分写会成为 O(n²) 热点。
    size_t off = 0;
    while (off < _out.size()) {
        int n = sock_send_nb(_fd, _out.data() + off, _out.size() - off);
        if (n == -1) {
            close();
            return;
        } // 硬错误（含对端断开）→ 关闭连接
        if (n == 0)
            break; // would-block，等下次推进/WRITABLE
        off += static_cast<size_t>(n);
        touch(); // 发出字节 = 活动（重置 30s 空闲计时）
    }
    if (off > 0)
        _out.erase(0, off);
}

void Connection::flush_stream() {
    if (!_alive || !_stream)
        return;
    auto now = std::chrono::steady_clock::now();
    // 心跳：流缓冲空闲超过 heartbeat_interval 且无新帧 → 注入 `: ping` 注释帧，
    // 供客户端/中间层保活，并让下一次写失败暴露对端断开。
    if (_stream->empty() && now - _last_write >= heartbeat_interval) {
        _stream->ping();
    }
    if (!_stream->empty()) {
        _out += _stream->drain();
        // 输出缓冲上限：对端不读帧（写持续 would-block）时 _out 无界增长 → 关闭。
        if (_out.size() > kMaxOutBytes) {
            close();
            return;
        }
        _last_write = std::chrono::steady_clock::now();
    }
    // 即使没有新帧也重试积压输出（被 would-block 打断的写靠后续推进补完）。
    drain_out();
}

bool Connection::set_stream(std::shared_ptr<SseStream> sse) {
    if (!_alive || _stream || !sse)
        return false;
    _stream = std::move(sse);
    _last_write = std::chrono::steady_clock::now();
    touch();
    return true;
}

void Connection::push_sse_frame() {
    if (!_alive)
        return;
    if (_stream)
        flush_stream();
}

bool Connection::process(const Router& router) {
    if (!_alive)
        return false;

    // SSE 流模式：不再解析请求，只补完积压输出（写失败 → close 在 drain_out 内）。
    // 同时消费已就绪的输入（对端数据 / FIN）：否则对端 FIN 后 select 每轮都报告该 fd
    // 可读，drive → flush_stream 空转 → 连接永不回收（fd + hub 订阅泄漏 + poller 忙循环）。
    if (_stream) {
        bool had_out = !_out.empty();
        flush_stream();
        if (_alive && wait_readable(_fd, 0) != 0) { // 就绪探测：可读或错误
            std::string chunk;
            int n = sock_recv_nb(_fd, chunk, 64 * 1024);
            if (n == -2 || n == -1) { // EOF（哨兵 -2）或硬错误 → 收尾
                close();              // 触发 on_close → 控制器退订 → 无泄漏
                return had_out;
            }
            if (n == 0)
                return had_out; // would-block（0）：探针与 recv 间的
                                // 句柄复用窗口所致，非 FIN——不能关连接
            // n > 0：对端发了字节（流模式下不是合法 HTTP 请求）→ 丢弃，继续流。
            touch();
        }
        return had_out || !_out.empty();
    }

    bool progress = false;

    // 已定“输出清空后关闭”（对端 FIN / Connection: close / 400 / 413）：不再解析
    // 缓冲中的遗留字节，只补完输出；输出清空后关闭（L6 400 刷屏的根因修复——
    // 坏字节不再被反复解析，每连接至多一条 400）。
    if (_pending_eof) {
        drain_out();
        if (_out.empty())
            close();
        return false;
    }

    // 解析→按需增量读→再解析 循环。HttpParser 是增量无状态解析器：每次先试
    // 解析已缓冲字节；缺 body 或头部不完整时再从 socket 读一块、追加后重试，
    // 直到产出完整请求、出错、或暂时无更多数据。旧实现只在“头终结符尚未到达”
    // 时读，导致 body 落在后续 TCP 分段时永远 Incomplete 卡死。
    for (;;) {
        HttpRequest req;
        req.remote_addr = _remote; // 对端 IP：限流 key 与访问日志客户端 IP 字段
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
            // 剩余缓冲是下一条请求的起始字节（pipelining）→ 视为已部分到达；
            // 缓冲空 → 回到空闲 keep-alive（30s 计时）。
            _partial = !_in.empty();
            _sent_continue = false; // 下一条请求可重新获得 `100 Continue`
            touch();
            auto resp = router(req);
            // 请求行日志：流升级后 process() 走流分支（_stream）不再解析请求，此句在
            // 流连接上只在升级时执行一次（不逐帧刷屏），普通请求每请求记一次。
            // 路径消毒：客户端可控字节可能含控制字符 → 防日志注入。
            LOG_DEBUG("%s %s -> %d", method_name(req.method), sanitize_for_log(req.path).c_str(), resp.status);
            // Connection: close（HTTP/1.0 或显式 close）→ 复用 _pending_eof 机制：
            // 停止再读，输出清空后关闭。守卫保证缓冲中的遗留字节不再被解析。
            if (!req.keep_alive)
                _pending_eof = true;
            if (resp.is_stream) {
                // 升级为 SSE 流模式：写响应头，连接转入只写路径（push_sse_frame）。
                if (!_stream) {
                    _stream = std::make_shared<SseStream>(_id);
                    _last_write = std::chrono::steady_clock::now();
                    touch();
                }
                _out += resp.to_bytes();
                progress = true;
                if (_out.size() > kMaxOutBytes) {
                    close();
                    return progress;
                }
                break; // 流式连接脱离请求解析，不再读后续请求
            }
            _out += resp.to_bytes(req.keep_alive);
            progress = true;
            if (_out.size() > kMaxOutBytes) {
                close();
                return progress;
            }
            if (!req.keep_alive)
                break; // 关闭语义：不再解析管道遗留字节
            continue;  // pipeline: may be another complete request buffered
        }
        if (pr == ParseResult::BadRequest) {
            LOG_WARN("malformed request from conn %s", _id.c_str());
            _out += HttpResponse::bad_request("BAD_REQUEST", "malformed request").to_bytes(false);
            _pending_eof = true; // 坏字节不会消失：不再解析，输出清空后关闭
            break;
        }
        if (pr == ParseResult::EntityTooLarge) {
            LOG_WARN("request body too large from conn %s", _id.c_str());
            _out += HttpResponse::error(413, "BODY_TOO_LARGE", "request body too large").to_bytes(false);
            _pending_eof = true; // 超出上限的 body 无法消费 → 响应后关闭
            break;
        }
        // Incomplete → need more bytes from the socket. sock_recv_nb 的 0 既可能
        // 是 would-block 也可能是对端 FIN，先做就绪探测区分：不可读 → would-block，
        // 等下次推进；可读但 recv 仍返回 0 字节 → 真 FIN。
        if (req.expect_continue && !_sent_continue) {
            // Expect: 100-continue 且 body 尚未到齐 → 先发 100 让客户端尽快发 body。
            // 100 是原始线格式（无 Content-Length 等头），不走 to_bytes。
            _out += "HTTP/1.1 100 Continue\r\n\r\n";
            _sent_continue = true;
            drain_out();
        }
        if (wait_readable(_fd, 0) == 0)
            break;
        std::string chunk;
        int n = sock_recv_nb(_fd, chunk, 64 * 1024);
        if (n == -2) {
            _pending_eof = true;
            break;
        } // EOF 哨兵（recv 返回 0）
        if (n == -1) {
            close();
            return progress;
        } // 硬错误 → 关闭
        if (n == 0)
            break; // would-block：探针与 recv 间的
                   // 句柄复用窗口所致，等下次推进
        _in += chunk;
        _partial = true; // 收到过字节 → 请求已部分到达（慢读计时）
        touch();
        progress = true;
    }

    drain_out();
    // EOF 且输出已全部写出 → 收尾关闭（对端不会再发数据）。
    if (_pending_eof && _out.empty())
        close();
    return progress;
}

void Connection::touch() {
    _last_activity = std::chrono::steady_clock::now();
}

Connection::SweepAction Connection::sweep_check(std::chrono::steady_clock::time_point now) const {
    if (!_alive)
        return SweepAction::None;
    if (_stream) {
        // SSE 流模式：> heartbeat_interval 无帧写出 → 心跳 ping（flush 时注入
        // `: ping`；写失败 → drain_out 关闭，对端断开检测）。心跳本身会推进
        // _last_write/_last_activity，把下一次到期再推后 ~15s。
        if (now - _last_write >= heartbeat_interval)
            return SweepAction::Heartbeat;
        return SweepAction::None;
    }
    const auto idle = now - _last_activity;
    if (_partial) {
        // 请求已部分到达但 5s 无进展 → 慢客户端，关闭（有界服务）。
        return idle >= kStalledReadTimeout ? SweepAction::Close : SweepAction::None;
    }
    // 空闲 keep-alive（含从未发过数据的连接）→ 30s 超时关闭。
    return idle >= kKeepAliveTimeout ? SweepAction::Close : SweepAction::None;
}

} // namespace web
