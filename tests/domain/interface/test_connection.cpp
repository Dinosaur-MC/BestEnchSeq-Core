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
// Connection: close 语义（L1）：HTTP/1.0 默认关闭、HTTP/1.1 显式 `Connection:
// close` 关闭——响应带 `Connection: close`，写完后连接关闭（不复用），缓冲中
// 的遗留字节不再被解析。
// ---------------------------------------------------------------------------
static void test_connection_close_semantics() {
    for (const std::string& req : {"GET /ping HTTP/1.0\r\n\r\n",
                                   "GET /ping HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"}) {
        TcpListener l;
        expect(l.listen("127.0.0.1", 0), "listen");
        int client = sock_connect("127.0.0.1", l.bound_port());
        expect(client >= 0, "connect");
        int fd = l.accept();
        expect(fd >= 0, "accept");
        set_nonblocking(fd);
        set_nonblocking(client);
        Connection conn(fd, "id-close");
        StubRouter router;

        expect(sock_send(client, req), "send close-semantics request");
        std::string got;
        for (int i = 0; i < 100 && conn.alive(); ++i) {
            conn.process(router);
            std::string c;
            if (sock_recv_nb(client, c, 4096) > 0) got += c;
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        expect(got.find("pong") != std::string::npos, "request served");
        expect(got.find("Connection: close") != std::string::npos, "response says close");
        expect(!conn.alive(), "connection closed after response");

        sock_close(client);
        conn.close();
    }
}

// ---------------------------------------------------------------------------
// BadRequest（L6）：坏请求 → 恰好一条 400，且写完后连接关闭——坏字节不再每轮
// select 被反复解析、反复追加 400（旧的“400 刷屏”行为）。
// ---------------------------------------------------------------------------
static void test_bad_request_400_once_and_close() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "id-bad");
    StubRouter router;

    // "NOT A REQUEST"（带空格 → 请求行解析失败）→ BadRequest。
    expect(sock_send(client, "NOT A REQUEST\r\n\r\n"), "send garbage");
    std::string got;
    for (int i = 0; i < 200 && conn.alive(); ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    size_t n400 = 0;
    size_t p = 0;
    while ((p = got.find("HTTP/1.1 400", p)) != std::string::npos) {
        ++n400;
        p += 5;
    }
    expect(n400 == 1, "exactly one 400 (no repeated flood)");
    expect(!conn.alive(), "connection closed after 400");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Expect: 100-continue（L2d）：头先到（Expect + Content-Length，body 未到）→ 连接
// 发出原始 `HTTP/1.1 100 Continue` 且尚未派发最终响应；body 到达后正常响应，
// 100 只出现一次。
// ---------------------------------------------------------------------------
static void test_expect_100_continue() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "id-100");
    StubRouter router;

    std::string headers =
        "POST /echo HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n";
    expect(sock_send(client, headers), "send headers with Expect");
    std::string got;
    for (int i = 0; i < 100 && got.find("100 Continue") == std::string::npos; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(got.find("HTTP/1.1 100 Continue") != std::string::npos, "100 Continue sent");
    expect(got.find("200") == std::string::npos, "no final response before body arrives");

    // 收到 100 后客户端才发 body → 最终响应。
    expect(sock_send(client, "hello"), "send body after 100");
    for (int i = 0; i < 200 && got.find("hello") == std::string::npos; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(got.find("hello") != std::string::npos, "echo served after 100-continue");
    size_t n100 = 0;
    size_t p = 0;
    while ((p = got.find("HTTP/1.1 100 Continue", p)) != std::string::npos) {
        ++n100;
        p += 5;
    }
    expect(n100 == 1, "100 Continue sent exactly once");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Timeout sweep (I-3): Connection::sweep_check(now) with fabricated `now`
// offsets — deterministic, no sleeping. Idle keep-alive 30s → Close, partial
// request stalled 5s → Close, SSE stream idle 15s → Heartbeat.
// ---------------------------------------------------------------------------
static void test_timeout_sweep() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "id-sweep");
    StubRouter router;

    using Clock = std::chrono::steady_clock;
    const auto base = Clock::now();

    // 1) Fresh keep-alive connection with no data: idle → 30s close.
    expect(conn.sweep_check(base + std::chrono::seconds(29)) ==
               Connection::SweepAction::None,
           "idle keep-alive at 29s: None");
    expect(conn.sweep_check(base + std::chrono::seconds(31)) ==
               Connection::SweepAction::Close,
           "idle keep-alive at 31s: Close (30s cap)");

    // 2) Partial request received then stalled: 5s slow-read cap.
    const std::string partial = "GET /ping HT";  // 头未终结 → Incomplete
    expect(sock_send(client, partial), "send partial request");
    for (int i = 0; i < 50; ++i) {
        conn.process(router);  // 推进到把 partial 收进缓冲（_partial=true）
        if (conn.wants_read()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto after_partial = Clock::now();
    expect(conn.sweep_check(after_partial + std::chrono::seconds(4)) ==
               Connection::SweepAction::None,
           "stalled read at 4s: None");
    expect(conn.sweep_check(after_partial + std::chrono::seconds(6)) ==
               Connection::SweepAction::Close,
           "stalled read at 6s: Close (5s cap)");

    // 3) Completing the request resets to keep-alive idle semantics.
    const std::string rest = "TP/1.1\r\nHost: x\r\n\r\n";
    expect(sock_send(client, rest), "send rest of request");
    std::string got;
    for (int i = 0; i < 100 && got.find("pong") == std::string::npos; ++i) {
        conn.process(router);
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(got.find("pong") != std::string::npos, "completed request served");
    expect(conn.alive(), "alive after completed request");
    const auto after_complete = Clock::now();
    expect(conn.sweep_check(after_complete + std::chrono::seconds(29)) ==
               Connection::SweepAction::None,
           "completed conn at 29s: None (keep-alive)");
    expect(conn.sweep_check(after_complete + std::chrono::seconds(31)) ==
               Connection::SweepAction::Close,
           "completed conn at 31s: Close");

    // 4) SSE stream mode: idle past the heartbeat interval → Heartbeat
    //    (write-failure close happens on the ping flush, not in sweep_check).
    auto sse = std::make_shared<SseStream>("id-sweep");
    expect(conn.set_stream(sse), "set_stream accepted");
    const auto after_stream = Clock::now();
    expect(conn.sweep_check(after_stream + std::chrono::seconds(14)) ==
               Connection::SweepAction::None,
           "SSE idle at 14s: None (under 15s heartbeat)");
    expect(conn.sweep_check(after_stream + std::chrono::seconds(16)) ==
               Connection::SweepAction::Heartbeat,
           "SSE idle at 16s: Heartbeat");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    test_keepalive_two_requests();
    test_split_body_post();
    test_connection_close_semantics();
    test_bad_request_400_once_and_close();
    test_expect_100_continue();
    test_timeout_sweep();
    TEST_PASS("test_connection");
    return print_summary();
}
