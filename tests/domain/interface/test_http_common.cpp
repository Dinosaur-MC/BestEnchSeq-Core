// =============================================================================
// HttpCommon base types tests: percent-decode / query parse / MIME.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/interface/components/http/HttpCommon.h"
#include "framework/test_framework.h"
#include <string>

using namespace web;

static void test_percent_decode() {
    expect(percent_decode("a%20b") == "a b", "space");
    expect(percent_decode("minecraft%3Asharpness") == "minecraft:sharpness", "colon");
    expect(percent_decode("%2F") == "/", "slash");
    expect(percent_decode("a%zz") == "a%zz", "bad escape kept verbatim");
    expect(percent_decode("") == "", "empty");
}

static void test_query_parse() {
    QueryParams q = parse_query("a=1&b=hello%20world&flag");
    expect(q.get("a") == "1", "a");
    expect(q.get("b") == "hello world", "b decoded");
    expect(q.has("flag") && q.get("flag").empty(), "flag no value");
    expect(!q.has("missing"), "missing");
}

static void test_mime() {
    // mime_for() returns const char* — compare as std::string content (not pointers).
    expect(std::string(mime_for(".png")) == "image/png", "png");
    expect(std::string(mime_for(".html")) == "text/html", "html");
    expect(std::string(mime_for(".js")) == "text/javascript", "js");
    expect(std::string(mime_for(".xyz")) == "application/octet-stream", "unknown");
}

// Fix 1: is_stream responses must emit the STANDARD open-ended SSE head — no
// Content-Length, no Transfer-Encoding — so a browser EventSource does not wait
// for chunk framing that Connection::flush_stream never writes (raw SSE frames).
static void test_stream_head_shape() {
    HttpResponse r = sse_stream_response();
    std::string wire = r.to_bytes();
    expect(wire.find("HTTP/1.1 200 OK") != std::string::npos, "stream status line");
    expect(wire.find("Content-Type: text/event-stream") != std::string::npos, "stream content-type");
    expect(wire.find("Transfer-Encoding") == std::string::npos, "no Transfer-Encoding on SSE head");
    expect(wire.find("Content-Length") == std::string::npos, "no Content-Length on SSE head");
    expect(wire.find("Connection: keep-alive") != std::string::npos, "SSE keeps connection open");
    expect(wire.find("Cache-Control: no-cache") != std::string::npos, "SSE no-cache");
}

// The non-stream path must be unchanged: auto Content-Length = body size when
// absent, Connection: keep-alive, and no Transfer-Encoding ever.
static void test_non_stream_head_unchanged() {
    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = "application/json";
    r.body = R"({"ok":true})";
    std::string wire = r.to_bytes();
    expect(wire.find("Content-Length: " + std::to_string(r.body.size())) != std::string::npos,
           "non-stream auto Content-Length = body size");
    expect(wire.find("Connection: keep-alive") != std::string::npos, "non-stream keep-alive");
    expect(wire.find("Transfer-Encoding") == std::string::npos, "non-stream no Transfer-Encoding");
}

// keep_alive=false → `Connection: close` 替换硬编码 keep-alive；默认参数不变。
static void test_close_header() {
    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = "application/json";
    r.body = "{}";
    std::string wire = r.to_bytes(false);
    expect(wire.find("Connection: close") != std::string::npos, "close on keep_alive=false");
    expect(wire.find("Connection: keep-alive") == std::string::npos, "no keep-alive when closing");
    expect(r.to_bytes().find("Connection: keep-alive") != std::string::npos, "default keep-alive");
}

// 204 No Content：省略 Content-Type 行（避免 `Content-Type: \r\n`），仍带 Content-Length: 0。
static void test_no_content_head() {
    HttpResponse r = HttpResponse::no_content();
    std::string wire = r.to_bytes();
    expect(wire.find("HTTP/1.1 204 No Content") != std::string::npos, "204 status line");
    expect(wire.find("Content-Type") == std::string::npos, "204 omits Content-Type");
    expect(wire.find("Content-Length: 0") != std::string::npos, "204 has Content-Length: 0");
}

// 413 状态码有标准原因短语。
static void test_413_reason() {
    HttpResponse r = HttpResponse::error(413, "BODY_TOO_LARGE", "request body too large");
    std::string wire = r.to_bytes();
    expect(wire.find("HTTP/1.1 413 Payload Too Large") != std::string::npos, "413 reason phrase");
}

TEST_CASE("test_http_common") {
    test_percent_decode();
    test_query_parse();
    test_mime();
    test_stream_head_shape();
    test_non_stream_head_unchanged();
    test_close_header();
    test_no_content_head();
    test_413_reason();
    TEST_PASS("test_http_common");
}
