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
#include "common/io/json.h"
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

    // ── PATCH validation paths (restored from the pre-rewrite test) ──

    // Unknown language → 400 INVALID_FIELD.
    auto lang = app.call(Method::Patch, "/api/settings", R"({"lang":"zz_ZZ"})");
    expect(lang.status == 400 && lang.body.find("INVALID_FIELD") != std::string::npos,
           "unknown lang 400 INVALID_FIELD");

    // log_level out of range → 400 INVALID_FIELD.
    auto oob = app.call(Method::Patch, "/api/settings", R"({"log_level":9})");
    expect(oob.status == 400 && oob.body.find("INVALID_FIELD") != std::string::npos,
           "log_level 9 out of range 400");

    // Pathological wrap value 2^32+2 must be rejected, NOT wrap down to 2 (Fix 3).
    auto wrap = app.call(Method::Patch, "/api/settings", R"({"log_level":4294967298})");
    expect(wrap.status == 400 && wrap.body.find("INVALID_FIELD") != std::string::npos,
           "log_level 2^32+2 rejected (no int32 wrap)");

    // Persistence: a successful PATCH {"log_level":2} is reflected by GET.
    auto set2 = app.call(Method::Patch, "/api/settings", R"({"log_level":2})");
    expect(set2.status == 200, "set log_level 2");
    auto g2 = app.call(Method::Get, "/api/settings");
    expect(Json::parse(g2.body)["log_level"].as<int64_t>() == 2, "log_level persisted as 2");

    // A FAILED PATCH leaves state unchanged: after 400, GET still reports 2.
    auto fail = app.call(Method::Patch, "/api/settings", R"({"log_level":9})");
    expect(fail.status == 400, "failed log_level 9 patch is 400");
    auto g3 = app.call(Method::Get, "/api/settings");
    expect(Json::parse(g3.body)["log_level"].as<int64_t>() == 2,
           "failed patch left log_level unchanged");

    // Malformed JSON body → 400 INVALID_BODY (Router maps JsonException → 400).
    auto mal = app.call(Method::Patch, "/api/settings", "{not json");
    expect(mal.status == 400 && mal.body.find("code") != std::string::npos,
           "malformed JSON body 400");

    // Non-object body → 400 INVALID_FIELD.
    auto arr = app.call(Method::Patch, "/api/settings", "[1,2]");
    expect(arr.status == 400 && arr.body.find("INVALID_FIELD") != std::string::npos,
           "non-object body 400 INVALID_FIELD");
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
