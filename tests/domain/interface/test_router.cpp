// tests/domain/interface/test_router.cpp
#include "domain/interface/components/http/Router.h"
#include "domain/interface/components/http/HttpController.h"
#include "framework/test_utils.h"
#include <string>

using namespace web;

namespace {
class DemoCtl : public HttpController<DemoCtl> {
public:
    using Self = DemoCtl;
    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/demo", list),
            BESQ_ROUTE(Get, "/demo/{id}", get),
            BESQ_ROUTE(Post, "/demo", create),
        };
    }
    Response list() { return Response::json(200, "OK", R"({"kind":"list"})"); }
    Response get(const HttpRequest&, const PathParams& p) {
        return Response::json(200, "OK", "{\"kind\":\"get\",\"id\":\"" + p.get("id") + "\"}");
    }
    Response create(const HttpRequest&, const Json&) { return Response::json(201, "Created", "{}"); }
};

// 编译期负面用例（consteval 求值）
static_assert(validate_routes(std::array{
    ConstRouteDef{Method::Get, "/x/{id}", nullptr},
    ConstRouteDef{Method::Get, "/x/detail", nullptr},
}) == false, "param+literal same level (same method) rejected");
static_assert(validate_routes(std::array{
    ConstRouteDef{Method::Get, "/x/{id}", nullptr},
    ConstRouteDef{Method::Post, "/x/detail", nullptr},
}) == true, "cross-method same level allowed");
static_assert(validate_routes(std::array{
    ConstRouteDef{Method::Get, "/dup", nullptr},
    ConstRouteDef{Method::Get, "/dup", nullptr},
}) == false, "duplicate rejected");
static_assert(validate_routes(std::array{
    ConstRouteDef{Method::Get, "/a/{id}/sub", nullptr},
    ConstRouteDef{Method::Get, "/a/{id}/other", nullptr},
}) == true, "same-level literals under param OK");
static_assert(validate_routes(std::array{
    ConstRouteDef{Method::Get, "/demo/{}", nullptr},
}) == false, "empty param name rejected");

void test_ok() {
    Router r;
    r.register_controller<DemoCtl>();
    HttpRequest req; req.method = Method::Get; req.path = "/demo";
    auto resp = r.dispatch(req);
    expect(resp.status == 200 && resp.body.find("list") != std::string::npos, "list");
    req.path = "/demo/42";
    auto resp2 = r.dispatch(req);
    expect(resp2.status == 200 && resp2.body.find("\"id\":\"42\"") != std::string::npos, "param");
    req.method = Method::Post; req.path = "/demo"; req.body = "{}";
    expect(r.dispatch(req).status == 201, "create");
}

void test_404_405() {
    Router r;
    r.register_controller<DemoCtl>();
    HttpRequest req; req.method = Method::Get; req.path = "/nope";
    auto nf = r.dispatch(req);
    expect(nf.status == 404, "unknown path 404");
    req.path = "/demo";
    req.method = Method::Delete;
    auto mt = r.dispatch(req);
    expect(mt.status == 405 && mt.header_value("Allow").find("GET") != std::string::npos, "405 + Allow");
}

void test_percent_decode_path() {
    Router r;
    r.register_controller<DemoCtl>();
    HttpRequest req; req.method = Method::Get; req.path = "/demo/42%2F7";
    auto resp = r.dispatch(req);
    expect(resp.status == 200 && resp.body.find("42/7") != std::string::npos, "decoded param");
}

void test_bad_body_400() {
    Router r;
    r.register_controller<DemoCtl>();
    HttpRequest req; req.method = Method::Post; req.path = "/demo"; req.body = "{not json";
    auto resp = r.dispatch(req);
    expect(resp.status == 400, "malformed body -> 400");
    expect(resp.body.find("INVALID_BODY") != std::string::npos, "code INVALID_BODY");
}
} // namespace

int main() {
    test_ok();
    test_404_405();
    test_percent_decode_path();
    test_bad_body_400();
    TEST_PASS("test_router");
    return print_summary();
}
