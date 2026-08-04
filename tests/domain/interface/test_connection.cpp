// =============================================================================
// Connection state machine tests (components/http, namespace web)
//
// Drives a real socket pair: non-blocking read -> incremental parse -> Router
// dispatch -> non-blocking write, and verifies:
//   - two requests can be served on one keep-alive connection;
//   - a POST whose headers and body arrive in SEPARATE sends is still served
//     (the body is consumed by a later read, not stalled forever).
// =============================================================================

#include "domain/interface/components/http/Connection.h"
#include "domain/interface/components/http/Socket.h"
#include "framework/test_utils.h"
#include <chrono>
#include <string>
#include <thread>

using namespace web;

namespace {
// Stub dispatcher: convertible to Connection::Router (std::function) via
// operator(). /ping -> 200, /echo -> 200 with the request body echoed back,
// any other path -> 404.
class StubRouter {
public:
    HttpResponse operator()(const HttpRequest& req) const {
        if (req.path == "/ping")
            return HttpResponse::json(200, "OK", R"({"pong":true})");
        if (req.path == "/echo")
            return HttpResponse::json(200, "OK", req.body);
        return HttpResponse::not_found();
    }
};
} // namespace

// ---------------------------------------------------------------------------
// Two requests on the same keep-alive connection.
// ---------------------------------------------------------------------------
static void test_keepalive_two_requests() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "id-1");
    StubRouter router;

    // 请求 1
    std::string req1 = "GET /ping HTTP/1.1\r\nHost: x\r\n\r\n";
    expect(sock_send(client, req1), "send req 1");
    std::string got;
    // process() 是非阻塞推进：反复调用直到收到响应 1 或超时。
    for (int i = 0; i < 100 && got.find("pong") == std::string::npos; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(got.find("pong") != std::string::npos, "resp 1");
    expect(got.find("Connection: close") == std::string::npos, "keep-alive (no close)");

    // 请求 2（同一连接）
    expect(sock_send(client, req1), "send req 2");
    size_t p1 = got.find("pong");
    for (int i = 0; i < 100 && conn.alive() && got.find("pong", p1 + 1) == std::string::npos; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(got.find("pong", p1 + 1) != std::string::npos, "resp 2 on same conn");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// POST whose HEADERS and BODY arrive in separate sends (split body).
//
// Regression for the read-guard defect: the old state machine read only while
// the header terminator was NOT yet buffered, so a body arriving in a later
// TCP segment than the headers left the parser Incomplete forever and the
// connection stalled without ever consuming the body.
//
// We force the split deterministically: send only the headers, drive process()
// until the server has buffered them (parser must stay Incomplete, no response),
// and only then send the body. The response must still arrive.
// ---------------------------------------------------------------------------
static void test_split_body_post() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "id-split");
    StubRouter router;

    // 阶段 1：只发请求头（Content-Length: 5 但 body 尚未到达），并反复推进，
    // 让服务器只读到头部 → 解析必须停在 Incomplete，且不产生任何响应。
    std::string headers =
        "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n";
    expect(sock_send(client, headers), "send headers only");
    std::string got;
    for (int i = 0; i < 50; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(got.empty(), "no response before body arrives");

    // 阶段 2：单独发送 body（模拟落在后续 TCP 分段），循环直到响应到达。
    expect(sock_send(client, "hello"), "send body separately");
    for (int i = 0; i < 200 && got.find("hello") == std::string::npos; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(got.find("hello") != std::string::npos, "response echoes split body");
    expect(conn.alive(), "conn alive after split body");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    test_keepalive_two_requests();
    test_split_body_post();
    TEST_PASS("test_connection");
    return print_summary();
}
