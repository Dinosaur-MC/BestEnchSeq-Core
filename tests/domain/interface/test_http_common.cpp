// =============================================================================
// HttpCommon base types tests: percent-decode / query parse / MIME.
// =============================================================================

#include "domain/interface/components/http/HttpCommon.h"
#include "framework/test_utils.h"
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

int main() {
    test_percent_decode();
    test_query_parse();
    test_mime();
    test_stream_head_shape();
    test_non_stream_head_unchanged();
    TEST_PASS("test_http_common");
    return print_summary();
}
