// =============================================================================
// SSE stream tests (components/http, namespace web)
//
// SseStream is the per-connection SSE frame buffer: event+data frames, a
// comment heartbeat frame (`: ping`), and drain() to take the whole buffer.
// The Connection streaming integration is exercised over a real socket pair:
//   - set_stream upgrades the connection to stream mode (wants_read off,
//     second set_stream rejected);
//   - frames accumulated in the stream reach the wire as SSE text after
//     push_sse_frame();
//   - an idle stream connection flushes a heartbeat ping on the next flush;
//   - an is_stream response switches a request-handling connection to stream
//     mode and writes the text/event-stream response head;
//   - a peer that disappears is detected on the next failed write (close).
// =============================================================================

#include "domain/interface/components/http/SseStream.h"
#include "domain/interface/components/http/Connection.h"
#include "domain/interface/components/http/Socket.h"
#include "framework/test_utils.h"
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace web;

namespace {
// Stub dispatcher: GET /events -> SSE stream response; otherwise 404.
class StubRouter {
public:
    HttpResponse operator()(const HttpRequest& req) const {
        if (req.path == "/events") return sse_stream_response();
        return HttpResponse::not_found();
    }
};

// Drive a non-blocking read on the client until `needle` shows up in `got`.
void recv_until(int client, std::string& got, const char* needle, int max_tries = 100) {
    for (int i = 0; i < max_tries && got.find(needle) == std::string::npos; ++i) {
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
} // namespace

// ---------------------------------------------------------------------------
// SseStream frame buffer: event/data frames + ping comment + drain().
// ---------------------------------------------------------------------------
static void test_frame_and_heartbeat() {
    SseStream sse("ev-1");
    sse.frame("progress", R"({"progress":0.5})");
    sse.frame("completed", R"({"result":{}})");
    sse.ping();
    expect(!sse.empty(), "buffer non-empty before drain");
    auto data = sse.drain();
    expect(data.find("data: {\"progress\":0.5}") != std::string::npos, "progress frame");
    expect(data.find("data: {\"result\":{}}") != std::string::npos, "completed frame");
    expect(data.find(": ping") != std::string::npos, "heartbeat comment");
    expect(data.find("event:") != std::string::npos || data.find("data:") != std::string::npos,
           "has frames");
    expect(data.find("event: progress") != std::string::npos, "progress event type present");
    expect(data.find("event: completed") != std::string::npos, "completed event type present");
    expect(data.find("\n\n") != std::string::npos, "frames blank-line terminated");
    expect(sse.empty(), "buffer cleared after drain");
}

// ---------------------------------------------------------------------------
// set_stream: upgrade + rejection of a second stream + wants_read off.
// ---------------------------------------------------------------------------
static void test_set_stream_semantics() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "sse-sem");

    auto sse = std::make_shared<SseStream>("sse-sem");
    expect(conn.set_stream(sse), "set_stream accepted");
    expect(conn.set_stream(std::make_shared<SseStream>("x")) == false, "second set_stream rejected");
    expect(conn.streaming(), "connection reports stream mode");
    expect(!conn.wants_read(), "stream connection stops reading requests");
    expect(conn.alive(), "still alive in stream mode");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Frames reach the wire: accumulate into the stream, flush via push_sse_frame.
// ---------------------------------------------------------------------------
static void test_stream_frames_on_wire() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "sse-frames");

    auto sse = std::make_shared<SseStream>("sse-frames");
    expect(conn.set_stream(sse), "set_stream accepted");

    sse->frame("progress", R"({"progress":0.5})");
    sse->frame("completed", R"({"result":{}})");
    conn.push_sse_frame();
    std::string got;
    recv_until(client, got, "completed");
    expect(got.find("event: progress") != std::string::npos, "progress event type on wire");
    expect(got.find("data: {\"progress\":0.5}") != std::string::npos, "progress frame on wire");
    expect(got.find("data: {\"result\":{}}") != std::string::npos, "completed frame on wire");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Heartbeat: an idle stream (empty buffer, interval elapsed) flushes a ping.
// ---------------------------------------------------------------------------
static void test_stream_heartbeat_ping() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "sse-hb");

    auto sse = std::make_shared<SseStream>("sse-hb");
    expect(conn.set_stream(sse), "set_stream accepted");
    conn.heartbeat_interval = std::chrono::milliseconds(0);   // force immediate heartbeat

    conn.push_sse_frame();   // idle stream -> ping comment frame flushed
    std::string got;
    recv_until(client, got, ": ping");
    expect(got.find(": ping") != std::string::npos, "heartbeat ping flushed on idle stream");
    expect(got.find("\n\n") != std::string::npos, "ping is a standalone comment frame");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// A request whose handler returns an SSE response switches the connection to
// stream mode and writes the text/event-stream response head.
// ---------------------------------------------------------------------------
static void test_stream_response_enters_stream_mode() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "sse-req");
    StubRouter router;

    expect(sock_send(client, "GET /events HTTP/1.1\r\nHost: x\r\n\r\n"), "send GET /events");
    for (int i = 0; i < 100 && !conn.streaming(); ++i) conn.process(router);
    expect(conn.streaming(), "is_stream response enters stream mode");
    expect(!conn.wants_read(), "stream mode stops reading requests");
    expect(conn.alive(), "connection stays open in stream mode");

    std::string got;
    recv_until(client, got, "text/event-stream");
    expect(got.find("HTTP/1.1 200 OK") != std::string::npos, "stream status line");
    expect(got.find("text/event-stream") != std::string::npos, "stream content-type");

    sock_close(client);
    conn.close();
}

// ---------------------------------------------------------------------------
// Peer FIN reaps an idle stream connection (Fix 2). A client that closes (FIN)
// with no in-flight write must still be detected: before the fix the stream
// branch never read, so the FIN stayed pending → select reported the fd readable
// every poll round → drive → flush_stream no-op → busy-loop + fd/hub leak forever.
// ---------------------------------------------------------------------------
static void test_stream_fin_closes() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "sse-fin");

    auto sse = std::make_shared<SseStream>("sse-fin");
    expect(conn.set_stream(sse), "set_stream accepted");
    expect(conn.alive(), "stream connection alive before FIN");

    bool close_fired = false;
    conn.on_close([&] { close_fired = true; });

    sock_close(client);                                    // 对端 FIN（无任何在途写）
    bool closed = false;
    for (int i = 0; i < 200 && conn.alive(); ++i) {
        conn.process(StubRouter{});                        // 流模式：消费 FIN → 关闭
        if (!conn.alive()) { closed = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(closed, "peer FIN closes the stream connection");
    expect(!conn.alive(), "connection not alive after FIN");
    expect(close_fired, "on_close fired after FIN close");

    conn.close();
}

// ---------------------------------------------------------------------------
// Disconnect detection: the peer goes away; the next failed write closes the
// connection. A 2 MiB frame cannot be fully buffered by the socket, so retrying
// the blocked write (stream-mode process) must surface a send error and close.
// ---------------------------------------------------------------------------
static void test_stream_disconnect_closes() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    // Linux 自动调优发送缓冲可以容纳整个 2 MiB 帧（write 永不失败 → 连接
    // 永不关闭）；Windows 默认缓冲小得多。显式收窄 SO_SNDBUF 消除平台差异：
    // 帧必然部分写入、阻塞在缓冲上，重试路径必然触发（对端 RST 后 write
    // 失败 → 关闭），与平台发送缓冲大小无关。
    expect(set_send_buffer(fd, 8 * 1024), "shrink sndbuf");
    Connection conn(fd, "sse-disc");

    auto sse = std::make_shared<SseStream>("sse-disc");
    expect(conn.set_stream(sse), "set_stream accepted");

    sock_close(client);                                  // peer disappears
    sse->frame("bulk", std::string(2 << 20, 'x'));       // exceeds the send buffer
    conn.push_sse_frame();                               // first flush fills the buffer
    // The peer's RST can land between two writes of the FIRST flush (Linux
    // send buffers are larger than the shrunk Windows ones), so the write
    // failure may close the connection inside push_sse_frame itself — both
    // that path and the retry-in-process path satisfy the contract.
    bool died = !conn.alive();
    for (int i = 0; i < 5000 && conn.alive() && !died; ++i) {
        conn.process(StubRouter{});                      // stream mode -> retry blocked write
        if (!conn.alive()) { died = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(died, "write failure after client disconnect closes the connection");

    conn.close();
}

int main() {
    test_frame_and_heartbeat();
    test_set_stream_semantics();
    test_stream_frames_on_wire();
    test_stream_heartbeat_ping();
    test_stream_response_enters_stream_mode();
    test_stream_disconnect_closes();
    test_stream_fin_closes();
    TEST_PASS("test_sse_stream");
    return print_summary();
}
