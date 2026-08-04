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
    bool did = false;

    // 读：缓冲里没有完整请求头时才尝试读（避免无限读积压）。
    if (_in.find("\r\n\r\n") == std::string::npos) {
        std::string chunk;
        int n = sock_recv_nb(_fd, chunk, 64 * 1024);
        if (n == -1) { close(); return false; }          // 错误/对端关闭
        if (n == 0) {
            if (_in.empty()) { close(); return false; }  // EOF 且无任何数据 → 关闭
            // EOF 但已有半请求：交给解析，Incomplete 则关闭。
            _pending_eof = true;
        } else {
            _in += chunk;
            did = true;
        }
    }

    // 解析循环：一次可能处理缓冲里的多条 keep-alive 请求。
    for (;;) {
        HttpRequest req;
        size_t consumed = 0;
        auto pr = _parser.parse(_in, consumed, req);
        if (pr == ParseResult::Incomplete) {
            if (_pending_eof) close();           // 对端关闭且请求不完整 → 关闭
            break;
        }
        if (pr == ParseResult::BadRequest) {
            _out += HttpResponse::bad_request("BAD_REQUEST", "malformed request").to_bytes();
            did = true;
            break;
        }
        _in.erase(0, consumed);
        auto resp = router(req);
        _out += resp.to_bytes();
        did = true;
        if (resp.is_stream) break;               // 流式连接脱离本状态机
    }

    drain_out();
    // EOF 且输出已全部写出 → 收尾关闭（对端不会再发数据）。
    if (_pending_eof && _out.empty()) close();
    return did;
}

} // namespace web
