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

int main() {
    test_percent_decode();
    test_query_parse();
    test_mime();
    TEST_PASS("test_http_common");
    return print_summary();
}
