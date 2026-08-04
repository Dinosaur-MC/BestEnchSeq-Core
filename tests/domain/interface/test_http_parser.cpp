// =============================================================================
// HttpParser tests (components/http, namespace web)
//
// Incremental HTTP/1.1 request parsing: raw (encoded) path, decoded query
// params, keep-alive tail parsing, header/body size limits.
// =============================================================================

#include "domain/interface/components/http/HttpParser.h"
#include "framework/test_utils.h"
#include <string>

using namespace web;

// ---------------------------------------------------------------------------
// Basic parse: method / path / header / consumed bytes.
// ---------------------------------------------------------------------------
static void test_basic_parse() {
    std::string buf = "GET /foo HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::Complete, "complete");
    expect(req.method == Method::Get, "method");
    expect(req.path == "/foo", "path");
    expect(req.header("Host") == "x", "header");
    expect(consumed == buf.size(), "consumed all");
}

// ---------------------------------------------------------------------------
// Raw (encoded) path + decoded query params.
// Path stays raw: {param} segments are percent-decoded per-segment by
// Router::match_segments at dispatch time, so %2F survives as data within a
// param instead of becoming a separator.
// ---------------------------------------------------------------------------
static void test_decode_and_query() {
    std::string buf = "GET /api/profiles/minecraft%3Asharpness?since=5&limit=10 HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    auto pr = parse_incremental(buf, consumed, req);
    expect(pr == ParseResult::Complete, "complete");
    expect(req.path == "/api/profiles/minecraft%3Asharpness", "path stays raw (encoded)");
    expect(req.query.get("since") == "5" && req.query.get("limit") == "10", "query");
}

// ---------------------------------------------------------------------------
// keep-alive: a second request trailing the first in the same buffer.
// ---------------------------------------------------------------------------
static void test_keepalive_tail() {
    std::string buf = "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::Complete, "first");
    expect(req.path == "/a", "a");
    HttpRequest req2;
    size_t consumed2 = 0;
    std::string rest = buf.substr(consumed);
    expect(parse_incremental(rest, consumed2, req2) == ParseResult::Complete, "second");
    expect(req2.path == "/b", "b");
}

// ---------------------------------------------------------------------------
// Oversized header block -> BadRequest (431 semantics).
// ---------------------------------------------------------------------------
static void test_header_too_large() {
    std::string buf(80 * 1024, 'a');
    buf = "GET /x HTTP/1.1\r\n" + buf + "\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::BadRequest, "431");
}

// ---------------------------------------------------------------------------
// Unterminated oversized header block -> BadRequest (431 protection).
// Unlike test_header_too_large, this buffer has NO \r\n\r\n terminator, so the
// >64KB-unterminated-buffer guard must reject it (not wait forever).
// ---------------------------------------------------------------------------
static void test_unterminated_header_too_large() {
    // >64KB header with NO \r\n\r\n terminator → parser must reject (431 protection)
    std::string buf(80 * 1024, 'a');
    HttpRequest req; size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::BadRequest,
           "unterminated oversized header -> BadRequest");
}

// ---------------------------------------------------------------------------
// Content-Length body round-trip.
// ---------------------------------------------------------------------------
static void test_body() {
    std::string buf = "POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::Complete, "complete");
    expect(req.method == Method::Post, "post method");
    expect(req.body == "hello", "body");
    expect(consumed == buf.size(), "consumed all");
}

// ---------------------------------------------------------------------------
// Partial request (headers not terminated) -> Incomplete.
// ---------------------------------------------------------------------------
static void test_incomplete() {
    std::string buf = "GET /x HTTP/1.1\r\nHost: x";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::Incomplete, "incomplete");
}

// ---------------------------------------------------------------------------
// Unknown request method -> BadRequest.
// ---------------------------------------------------------------------------
static void test_bad_method() {
    std::string buf = "BREW /x HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::BadRequest, "bad method");
}

// ---------------------------------------------------------------------------
// Content-Length over the 1 MiB body cap -> BadRequest (413 semantics).
// ---------------------------------------------------------------------------
static void test_body_too_large() {
    std::string buf = "POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 2097152\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::BadRequest, "413");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    test_basic_parse();
    test_decode_and_query();
    test_keepalive_tail();
    test_header_too_large();
    test_unterminated_header_too_large();
    test_body();
    test_incomplete();
    test_bad_method();
    test_body_too_large();
    TEST_PASS("test_http_parser");
    return print_summary();
}
