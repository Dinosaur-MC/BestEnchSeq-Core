// tests/domain/interface/test_route_def.cpp
#define BESQ_TEST_MAIN
#include "domain/interface/components/http/HttpCommon.h"
#include "domain/interface/components/http/RouteDef.h"
#include "framework/test_framework.h"
#include <array>
#include <string>

using namespace web; // 测试内使用未限定的 Response/HttpRequest/PathParams/Method/Json

namespace {
class DemoCtl {
public:
    using Self = DemoCtl;
    int hits = 0;
    Response no_args() {
        ++hits;
        return Response::json(200, "OK", "{}");
    }
    Response with_req(const HttpRequest&) {
        ++hits;
        return Response::json(200, "OK", "{}");
    }
    Response with_params(const HttpRequest&, const PathParams&) {
        ++hits;
        return Response::json(200, "OK", "{}");
    }
    Response with_body(const HttpRequest& r, const Json&) {
        ++hits;
        (void)r;
        return Response::json(200, "OK", "{}");
    }
    Response with_both(const HttpRequest& r, const PathParams&, const Json&) {
        ++hits;
        (void)r;
        return Response::json(200, "OK", "{}");
    }
};

using Self = DemoCtl; // BESQ_ROUTE 宏展开引用 Self::H，需在宏使用处可见

constexpr auto kTable = std::array{
    BESQ_ROUTE(Get, "/a", no_args),    BESQ_ROUTE(Post, "/b", with_req),      BESQ_ROUTE(Get, "/c/{id}", with_params),
    BESQ_ROUTE(Post, "/d", with_body), BESQ_ROUTE(Put, "/e/{id}", with_both),
};
static_assert(kTable.size() == 5, "route count");
static_assert(kTable[0].method == Method::Get, "method");
static_assert(kTable[2].pattern == "/c/{id}", "pattern");

void test_invoke_all_forms() {
    DemoCtl ctl;
    HttpRequest req;
    req.body = "{}";
    PathParams pp;
    pp.kv.emplace_back("id", "42");
    expect(kTable[0].invoke(&ctl, req, pp).status == 200, "no_args");
    expect(kTable[1].invoke(&ctl, req, pp).status == 200, "with_req");
    expect(kTable[2].invoke(&ctl, req, pp).status == 200, "with_params");
    expect(kTable[3].invoke(&ctl, req, pp).status == 200, "with_body");
    expect(kTable[4].invoke(&ctl, req, pp).status == 200, "with_both");
    expect(ctl.hits == 5, "all five handlers ran");
}
} // namespace

TEST_CASE("test_route_def") {
    test_invoke_all_forms();
    TEST_PASS("test_route_def");
}
