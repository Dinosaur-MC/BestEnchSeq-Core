// =============================================================================
// HTTP server tests: parser + socket + end-to-end server behavior.
// =============================================================================
#include "domain/interface/web/http/HttpCommon.h"
#include "domain/interface/web/http/HttpParser.h"
#include "framework/test_utils.h"
#include <string>

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

int main() {
    try {
        test_parse_get();
        test_parse_post_body();
        test_parse_incomplete();
        test_parse_bad_request_line();
        test_response_serialize();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
