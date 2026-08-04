// src/domain/interface/components/http/Connection.cpp
#include "Connection.h"
#include "Socket.h"

namespace web {

Connection::Connection(int fd, std::string id) : _fd(fd), _id(std::move(id)) {}

Connection::~Connection() { close(); }

void Connection::close() {
    if (_alive) {
        _alive = false;
        sock_close(_fd);
        _fd = -1;
    }
}

void Connection::drain_out() {
    while (!_out.empty()) {
        int n = sock_send_nb(_fd, _out);
        if (n <= 0) break;                       // would-block 或错误，等下次推进
        _out.erase(0, static_cast<size_t>(n));
    }
}

bool Connection::process(const Router& router) {
    if (!_alive) return false;
    bool progress = false;

    // 解析→按需增量读→再解析 循环。HttpParser 是增量无状态解析器：每次先试
    // 解析已缓冲字节；缺 body 或头部不完整时再从 socket 读一块、追加后重试，
    // 直到产出完整请求、出错、或暂时无更多数据。旧实现只在“头终结符尚未到达”
    // 时读，导致 body 落在后续 TCP 分段时永远 Incomplete 卡死。
    for (;;) {
        HttpRequest req;
        size_t consumed = 0;
        auto pr = _parser.parse(_in, consumed, req);
        if (pr == ParseResult::Complete) {
            _in.erase(0, consumed);
            auto resp = router(req);
            if (resp.is_stream) { /* SSE handled by SseStream path in a later task; not expected here yet */ }
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
