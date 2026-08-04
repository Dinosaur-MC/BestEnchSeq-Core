// =============================================================================
// Web API tests (modern controllers): Health/Status/Settings/Profiles over
// web::Router. Algorithm/Calculator/Logs controllers arrive in later tasks and
// extend this file.
// =============================================================================
#include "domain/interface/web/controllers/HealthController.h"
#include "domain/interface/web/controllers/StatusController.h"
#include "domain/interface/web/controllers/SettingsController.h"
#include "domain/interface/web/controllers/ProfilesController.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/BesqContext.h"
#include "common/io/json.h"
#include "framework/test_utils.h"
#include <memory>
#include <mutex>
#include <string>

using namespace web;

namespace {
struct TestApp {
    Router router;
    std::mutex gate;
    BesqContext& ctx;
    explicit TestApp(BesqContext& c) : ctx(c) {
        router.register_controller<HealthController>();
        router.register_controller<StatusController>(c);
        router.register_controller<SettingsController>(c);
        router.register_controller<ProfilesController>(ctx, gate);
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

void test_profiles(TestApp& app) {
    const std::string key = app.ctx.list_profiles()[0];

    // ── 1. list → 200 with "profiles" and "active" ──
    auto l = app.call(Method::Get, "/api/profiles");
    expect(l.status == 200 && l.body.find("profiles") != std::string::npos
               && l.body.find("active") != std::string::npos,
           "profiles list 200 + fields");

    // ── 2. read metadata → every ProfileMeta field present ──
    auto r = app.call(Method::Get, "/api/profiles/" + key);
    expect(r.status == 200, "profile metadata 200");
    for (const char* f : {"name", "dependencies", "is_root", "format",
                          "ench_count", "eq_count", "tag_count",
                          "version", "release_tag"})
        expect(r.body.find(f) != std::string::npos, std::string("metadata field ") + f);

    // ── 3. equipments round-trip: delete-existing → add → read → update → read → delete ──
    // `minecraft:netherite_sword` ships in the builtin profile, so the "add"
    // (which must be 201, not 409) needs the pre-existing entry removed first.
    auto pre = app.call(Method::Delete, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(pre.status == 204, "equip pre-delete existing 204");

    auto add = app.call(Method::Post, "/api/profiles/" + key + "/equipments",
                        R"({"id":"minecraft:netherite_sword","max_durability":2031})");
    expect(add.status == 201, "equip add 201");
    expect(add.header_value("Location").find("minecraft:netherite_sword") != std::string::npos,
           "equip add Location header");

    auto lst = app.call(Method::Get, "/api/profiles/" + key + "/equipments");
    expect(lst.status == 200 && lst.body.find("netherite_sword") != std::string::npos,
           "equip list contains added entry");

    auto upd = app.call(Method::Patch, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword",
                        R"({"id":"minecraft:netherite_sword","max_durability":5000})");
    expect(upd.status == 200, "equip update 200");

    auto rd = app.call(Method::Get, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(rd.status == 200 && rd.body.find("5000") != std::string::npos,
           "equip read reflects updated durability");

    auto del = app.call(Method::Delete, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(del.status == 204, "equip delete 204");
    auto gone = app.call(Method::Get, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(gone.status == 404, "equip read after delete 404");

    // ── 4. errors ──
    auto nope = app.call(Method::Get, "/api/profiles/nope");
    expect(nope.status == 404, "unknown profile 404");
    auto noench = app.call(Method::Get, "/api/profiles/" + key + "/enchantments/nope");
    expect(noench.status == 404, "unknown enchantment 404");
    auto dup = app.call(Method::Post, "/api/profiles",
                        R"({"source":")" + key + R"(","dest":")" + key + R"("})");
    expect(dup.status == 409, "create existing dest 409");
    auto bad = app.call(Method::Patch, "/api/profiles/" + key, R"({"dependencies":"x"})");
    expect(bad.status == 400, "patch bad dependencies 400");
    auto badobj = app.call(Method::Patch, "/api/profiles/" + key, "[1,2]");
    expect(badobj.status == 400, "patch non-object body 400");

    // ── 5. rename (on a fork, so the original key survives for later) ──
    auto fr = app.call(Method::Post, "/api/profiles",
                       R"({"source":")" + key + R"(","dest":")" + key + R"(-rs"})");
    expect(fr.status == 201, "fork for rename");
    auto rn = app.call(Method::Post, "/api/profiles/" + key + "-rs/rename",
                       R"({"name":")" + key + R"(-rd"})");
    expect(rn.status == 200, "rename 200");
    auto rr = app.call(Method::Get, "/api/profiles/" + key + "-rd");
    expect(rr.status == 200, "renamed profile readable");

    // ── 6. dependency update (on a scratch fork, so the root survives) ──
    auto fd = app.call(Method::Post, "/api/profiles",
                       R"({"source":")" + key + R"(","dest":")" + key + R"(-deps"})");
    expect(fd.status == 201, "fork for dependencies");

    // Empty dependency list → 200, then readback shows an empty array.
    auto pd = app.call(Method::Patch, "/api/profiles/" + key + "-deps",
                       R"({"dependencies":[]})");
    expect(pd.status == 200, "patch dependencies [] 200");
    auto gd = app.call(Method::Get, "/api/profiles/" + key + "-deps");
    expect(gd.status == 200, "deps profile readable");
    auto gd_json = Json::parse(gd.body);
    expect(gd_json["dependencies"].type() == JsonType::Array &&
               gd_json["dependencies"].as_array().empty(),
           "dependencies empty after [] patch");

    // Set an actual dependency on an existing profile; readback contains it.
    auto ps = app.call(Method::Patch, "/api/profiles/" + key + "-deps",
                       R"({"dependencies":[")" + key + R"("]})");
    expect(ps.status == 200, "patch dependencies [" + key + "] 200");
    auto gs = app.call(Method::Get, "/api/profiles/" + key + "-deps");
    expect(gs.status == 200, "deps profile readable after set");
    auto gs_json = Json::parse(gs.body);
    expect(gs_json["dependencies"].type() == JsonType::Array, "dependencies array present");
    bool has_key = false;
    for (const auto& d : gs_json["dependencies"].as_array())
        if (d.as<std::string>() == key)
            has_key = true;
    expect(has_key, "dependencies contains " + key);
}
} // namespace

int main() {
    BesqContext ctx; ctx.load_builtin(); ctx.load_profiles();
    TestApp app(ctx);
    test_health(app);
    test_status(app);
    test_settings(app);
    test_profiles(app);
    TEST_PASS("test_web_api");
    return print_summary();
}
