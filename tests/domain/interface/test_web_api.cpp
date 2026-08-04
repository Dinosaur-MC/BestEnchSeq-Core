// =============================================================================
// Web API tests (modern controllers): Health/Status/Settings over web::Router.
// Profiles/Algorithm/Calculator/Logs controllers arrive in later tasks and
// extend this file.
// =============================================================================
#include "domain/interface/web/controllers/HealthController.h"
#include "domain/interface/web/controllers/StatusController.h"
#include "domain/interface/web/controllers/SettingsController.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/BesqContext.h"
#include "framework/test_utils.h"
#include <memory>
#include <string>

using namespace web;

namespace {
struct TestApp {
    Router router;
    explicit TestApp(BesqContext& c) {
        router.register_controller<HealthController>();
        router.register_controller<StatusController>(c);
        router.register_controller<SettingsController>(c);
    }
    HttpResponse call(Method m, std::string path, std::string body = "") {
        HttpRequest req; req.method = m; req.path = std::move(path); req.body = std::move(body);
        return router.dispatch(req);
    }
};

void test_health(TestApp& app) {
    auto r = app.call(Method::Get, "/health");
    expect(r.status == 200, "health 200");
    expect(r.body.find("\"status\":\"ok\"") != std::string::npos, "status field");
    expect(r.body.find("uptime_ms") != std::string::npos, "uptime field");
}

void test_status(TestApp& app) {
    auto r = app.call(Method::Get, "/api/status");
    expect(r.status == 200, "status 200");
    for (const char* f : {"active_profile", "profile_count", "algorithm_count", "has_active_solve", "uptime_ms"})
        expect(r.body.find(f) != std::string::npos, std::string("field ") + f);
}

void test_settings(TestApp& app) {
    auto g = app.call(Method::Get, "/api/settings");
    expect(g.status == 200 && g.body.find("lang") != std::string::npos, "settings get");
    auto p = app.call(Method::Patch, "/api/settings", R"({"log_level":2})");
    expect(p.status == 200, "settings patch");
    auto bad = app.call(Method::Patch, "/api/settings", R"({"log_level":"x"})");
    expect(bad.status == 400 && bad.body.find("code") != std::string::npos, "bad field type 400");
    auto wrong = app.call(Method::Delete, "/api/settings");
    expect(wrong.status == 405 && wrong.header_value("Allow").find("GET") != std::string::npos, "405 + Allow");
}
} // namespace

int main() {
    BesqContext ctx; ctx.load_builtin(); ctx.load_profiles();
    TestApp app(ctx);
    test_health(app);
    test_status(app);
    test_settings(app);
    TEST_PASS("test_web_api");
    return print_summary();
}
