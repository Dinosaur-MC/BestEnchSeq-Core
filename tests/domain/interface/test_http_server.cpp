// =============================================================================
// HTTP server tests: request parser + response serialization.
// Socket / end-to-end server behavior lands with the accept loop (M0.2/M0.3).
// =============================================================================
#include "domain/interface/web/http/HttpCommon.h"
#include "domain/interface/web/http/HttpParser.h"
#include "domain/interface/web/http/Socket.h"
#include "framework/test_utils.h"
#include <string>
#include <thread>

using webhttp::HttpRequest;
using webhttp::HttpResponse;
using webhttp::HttpParser;
using webhttp::ParseResult;

static const char* GET_REQ =
    "GET /api/status?verbose=1 HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\n"
    "Accept: application/json\r\n"
    "\r\n";

static const char* POST_REQ =
    "POST /api/calculator HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 17\r\n"
    "\r\n"
    "{\"ok\":true,\"n\":3}";

void test_parse_get() {
    HttpRequest req;
    size_t consumed = 0;
    auto pr = HttpParser::parse(GET_REQ, consumed, req);
    expect(pr == ParseResult::Complete, "complete GET request parses");
    expect(req.method == "GET", "method is GET");
    expect(req.path == "/api/status", "path excludes query");
    expect(req.query == "verbose=1", "query string captured");
    expect(req.header("Host") == "127.0.0.1", "Host header captured");
    expect(req.body.empty(), "GET has no body");
    TEST_PASS("parse GET");
}

void test_parse_post_body() {
    HttpRequest req;
    size_t consumed = 0;
    auto pr = HttpParser::parse(POST_REQ, consumed, req);
    expect(pr == ParseResult::Complete, "complete POST request parses");
    expect(req.method == "POST", "method is POST");
    expect(req.path == "/api/calculator", "POST path");
    expect(req.body == "{\"ok\":true,\"n\":3}", "Content-Length body read exactly");
    TEST_PASS("parse POST body");
}

void test_parse_incomplete() {
    HttpRequest req;
    size_t consumed = 0;
    auto pr = HttpParser::parse("GET /api/status HTTP/1.1\r\nHost: x\r\n", consumed, req);
    expect(pr == ParseResult::Incomplete, "header block without CRLFCRLF is incomplete");
    TEST_PASS("parse incomplete");
}

void test_parse_bad_request_line() {
    HttpRequest req;
    size_t consumed = 0;
    auto pr = HttpParser::parse("NONSENSE\r\n\r\n", consumed, req);
    expect(pr == ParseResult::BadRequest, "malformed request line is bad request");
    TEST_PASS("parse bad request line");
}

void test_parse_zero_headers() {
    HttpRequest req;
    size_t consumed = 0;
    auto pr = HttpParser::parse("GET / HTTP/1.1\r\n\r\n", consumed, req);
    expect(pr == ParseResult::Complete, "zero-header request parses");
    expect(req.method == "GET", "method is GET");
    expect(req.path == "/", "path is /");
    expect(req.headers.empty(), "no headers captured");
    TEST_PASS("parse zero-header request");
}

void test_parse_negative_content_length() {
    HttpRequest req;
    size_t consumed = 0;
    const char* bad = "POST / HTTP/1.1\r\n"
                      "Content-Length: -5\r\n"
                      "\r\n";
    auto pr = HttpParser::parse(bad, consumed, req);
    expect(pr == ParseResult::BadRequest, "negative Content-Length is bad request");
    TEST_PASS("parse negative Content-Length");
}

void test_parse_body_incomplete() {
    HttpRequest req;
    size_t consumed = 0;
    const char* msg = "POST / HTTP/1.1\r\n"
                      "Content-Length: 100\r\n"
                      "\r\n"
                      "abc";
    auto pr = HttpParser::parse(msg, consumed, req);
    expect(pr == ParseResult::Incomplete, "Content-Length exceeding buffered body is incomplete");
    TEST_PASS("parse body incomplete");
}

void test_parse_non_numeric_content_length() {
    HttpRequest req;
    size_t consumed = 0;
    const char* bad = "POST / HTTP/1.1\r\n"
                      "Content-Length: abc\r\n"
                      "\r\n";
    auto pr = HttpParser::parse(bad, consumed, req);
    expect(pr == ParseResult::BadRequest, "non-numeric Content-Length is bad request");
    TEST_PASS("parse non-numeric Content-Length");
}

void test_parse_empty_method() {
    HttpRequest req;
    size_t consumed = 0;
    auto pr = HttpParser::parse(" / HTTP/1.1\r\n\r\n", consumed, req);
    expect(pr == ParseResult::BadRequest, "empty method is bad request");
    TEST_PASS("parse empty method");
}

void test_parse_oversized_headers() {
    HttpRequest req;
    size_t consumed = 0;
    std::string buf(70 * 1024, 'a'); // no CRLFCRLF terminator
    auto pr = HttpParser::parse(buf, consumed, req);
    expect(pr == ParseResult::BadRequest, "unterminated headers over 64 KiB are bad request");
    TEST_PASS("parse oversized headers");
}

void test_response_serialize() {
    HttpResponse resp;
    resp.status = 200;
    resp.body = "{}";
    auto bytes = resp.to_bytes();
    expect(bytes.find("HTTP/1.1 200 OK") == 0, "status line first");
    expect(bytes.find("Content-Type: application/json") != std::string::npos, "content-type header");
    expect(bytes.find("Content-Length: 2") != std::string::npos, "content-length header");
    expect(bytes.find("\r\n\r\n{}") != std::string::npos, "body after blank line");
    TEST_PASS("serialize response");
}

void test_socket_echo() {
    webhttp::TcpListener listener;
    expect(listener.listen("127.0.0.1", 0), "bind+listen on ephemeral port");
    expect(listener.bound_port() > 0, "bound port is non-zero");

    int server_fd = -1;
    std::thread server([&] {
        server_fd = listener.accept();
        if (server_fd >= 0) {
            std::string data;
            int n = webhttp::sock_recv(server_fd, data, 1024, 3000);
            if (n > 0) webhttp::sock_send(server_fd, data, 3000);
        }
    });

    int c = webhttp::sock_connect("127.0.0.1", listener.bound_port());
    expect(c >= 0, "client connects");
    expect(webhttp::sock_send(c, "ping", 3000), "client sends");
    std::string reply;
    expect(webhttp::sock_recv(c, reply, 1024, 3000) > 0, "client receives reply");
    expect(reply == "ping", "echo matches");
    webhttp::sock_close(c);
    server.join();
    if (server_fd >= 0) webhttp::sock_close(server_fd);
    TEST_PASS("socket echo round-trip");
}

void test_socket_refused() {
    // Connect to a port with nothing listening → must fail fast, not hang.
    int c = webhttp::sock_connect("127.0.0.1", 1);
    expect(c < 0, "connect to closed port fails");
    TEST_PASS("socket connect refused");
}

int main() {
    try {
        test_parse_get();
        test_parse_post_body();
        test_parse_incomplete();
        test_parse_bad_request_line();
        test_parse_zero_headers();
        test_parse_negative_content_length();
        test_parse_body_incomplete();
        test_parse_non_numeric_content_length();
        test_parse_empty_method();
        test_parse_oversized_headers();
        test_response_serialize();
        test_socket_echo();
        test_socket_refused();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
