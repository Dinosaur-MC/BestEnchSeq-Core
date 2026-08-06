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
// Content-Length over the 1 MiB body cap -> EntityTooLarge（413，与 400 区分）。
// ---------------------------------------------------------------------------
static void test_body_too_large() {
    std::string buf = "POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 2097152\r\n\r\n";
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental(buf, consumed, req) == ParseResult::EntityTooLarge, "413");
}

// ---------------------------------------------------------------------------
// Host 校验（RFC 9112）：HTTP/1.1 缺 Host 或多个 Host → BadRequest；HTTP/1.0 无 Host 合法。
// ---------------------------------------------------------------------------
static void test_host_required() {
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental("GET /x HTTP/1.1\r\n\r\n", consumed, req) == ParseResult::BadRequest,
           "HTTP/1.1 without Host -> BadRequest");
    expect(parse_incremental("GET /x HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", consumed, req) ==
               ParseResult::BadRequest,
           "duplicate Host -> BadRequest");
    expect(parse_incremental("GET /x HTTP/1.0\r\n\r\n", consumed, req) == ParseResult::Complete,
           "HTTP/1.0 without Host is legal");
}

// ---------------------------------------------------------------------------
// 重复 Content-Length → BadRequest（请求走私面，值相同也拒绝）。
// ---------------------------------------------------------------------------
static void test_duplicate_content_length_rejected() {
    HttpRequest req;
    size_t consumed = 0;
    std::string buf =
        "POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello";
    expect(parse_incremental(buf, consumed, req) == ParseResult::BadRequest,
           "duplicate Content-Length -> BadRequest");
}

// ---------------------------------------------------------------------------
// Transfer-Encoding（任何值）→ BadRequest（本服务器不支持 chunked，显式拒绝）。
// ---------------------------------------------------------------------------
static void test_transfer_encoding_rejected() {
    HttpRequest req;
    size_t consumed = 0;
    std::string buf = "POST /x HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n";
    expect(parse_incremental(buf, consumed, req) == ParseResult::BadRequest,
           "Transfer-Encoding -> BadRequest");
}

// ---------------------------------------------------------------------------
// Expect: 100-continue（大小写不敏感）→ expect_continue；其他 Expect 值 → BadRequest。
// ---------------------------------------------------------------------------
static void test_expect_continue() {
    HttpRequest req;
    size_t consumed = 0;
    // 头已到、body 未到 → Incomplete 且 expect_continue 置位（Connection 据此发 100）。
    std::string pending = "POST /x HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n";
    expect(parse_incremental(pending, consumed, req) == ParseResult::Incomplete, "waiting body");
    expect(req.expect_continue, "expect_continue set on Incomplete");
    // 值大小写不敏感 + 完整请求。
    std::string full =
        "POST /x HTTP/1.1\r\nHost: x\r\nExpect: 100-CONTINUE\r\nContent-Length: 5\r\n\r\nhello";
    HttpRequest req2;
    expect(parse_incremental(full, consumed, req2) == ParseResult::Complete, "full 100-continue");
    expect(req2.expect_continue, "expect_continue case-insensitive");
    // 无 body（CL 缺失/0）→ expect_continue 为 false。
    std::string no_body = "POST /x HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\n\r\n";
    HttpRequest req3;
    expect(parse_incremental(no_body, consumed, req3) == ParseResult::Complete, "no body");
    expect(!req3.expect_continue, "no body -> no 100 needed");
    // 其他 Expect 值 → BadRequest。
    HttpRequest req4;
    expect(parse_incremental("POST /x HTTP/1.1\r\nHost: x\r\nExpect: 200-ok\r\n\r\n", consumed,
                             req4) == ParseResult::BadRequest,
           "unsupported Expect -> BadRequest");
}

// ---------------------------------------------------------------------------
// keep_alive 计算：HTTP/1.1 默认保活、Connection: close 关闭；HTTP/1.0 默认关闭、
// Connection: keep-alive（大小写不敏感）保活。
// ---------------------------------------------------------------------------
static void test_keep_alive_computation() {
    HttpRequest req;
    size_t consumed = 0;
    expect(parse_incremental("GET /x HTTP/1.1\r\nHost: x\r\n\r\n", consumed, req) ==
               ParseResult::Complete &&
               req.keep_alive,
           "HTTP/1.1 default keep-alive");
    expect(parse_incremental("GET /x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", consumed,
                             req) == ParseResult::Complete &&
               !req.keep_alive,
           "HTTP/1.1 Connection: close");
    expect(parse_incremental("GET /x HTTP/1.0\r\n\r\n", consumed, req) == ParseResult::Complete &&
               !req.keep_alive,
           "HTTP/1.0 default close");
    expect(parse_incremental("GET /x HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n", consumed, req) ==
               ParseResult::Complete &&
               req.keep_alive,
           "HTTP/1.0 Connection: keep-alive (case-insensitive)");
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
    test_host_required();
    test_duplicate_content_length_rejected();
    test_transfer_encoding_rejected();
    test_expect_continue();
    test_keep_alive_computation();
    TEST_PASS("test_http_parser");
    return print_summary();
}
