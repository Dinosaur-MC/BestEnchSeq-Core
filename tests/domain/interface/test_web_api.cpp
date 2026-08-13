// =============================================================================
// Web API tests (modern controllers): Health/Status/Settings/Profiles over
// web::Router. Algorithm/Calculator/History controllers extend this file.
// =============================================================================
#define BESQ_TEST_MAIN
#include "AppConfig.h"
#include "common/io/json.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/components/http/StreamChannel.h"
#include "domain/interface/web/controllers/AlgorithmController.h"
#include "domain/interface/web/controllers/CalculatorController.h"
#include "domain/interface/web/controllers/FsController.h"
#include "domain/interface/web/controllers/HealthController.h"
#include "domain/interface/web/controllers/HistoryController.h"
#include "domain/interface/web/controllers/ProfilesController.h"
#include "domain/interface/web/controllers/SettingsController.h"
#include "domain/interface/web/controllers/StatusController.h"
#include "domain/interface/web/SseHub.h"
#include "domain/interface/web/WebModule.h"
#include "domain/interface/web/WebSolveService.h"
#include "framework/test_framework.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace web;

namespace {
struct TestApp {
    Router router;
    std::mutex gate;
    BesqContext& ctx;
    SseHub hub;
    std::unique_ptr<web::WebSolveService> solve;
    explicit TestApp(BesqContext& c) : ctx(c) {
        solve = std::make_unique<web::WebSolveService>(c, gate, &hub);
        router.register_controller<HealthController>();
        router.register_controller<StatusController>(c, gate);
        router.register_controller<SettingsController>(gate);
        router.register_controller<ProfilesController>(ctx, gate);
        router.register_controller<AlgorithmController>(c, *solve, gate);
        router.register_controller<CalculatorController>(*solve, hub);
        router.register_controller<FsController>();
        router.register_controller<HistoryController>(c);
    }
    HttpResponse call(Method m, std::string path, std::string body = "") {
        // Mirror HttpParser: split path from the ?query=... string (real requests
        // arrive with req.query already parsed).
        HttpRequest req;
        req.method = m;
        auto q = path.find('?');
        req.path = q == std::string::npos ? std::move(path) : path.substr(0, q);
        if (q != std::string::npos)
            req.query = parse_query(path.substr(q + 1));
        req.body = std::move(body);
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
    expect(lang.status == 400 && lang.body.find("INVALID_FIELD") != std::string::npos, "unknown lang 400 INVALID_FIELD");

    // log_level out of range → 400 INVALID_FIELD.
    auto oob = app.call(Method::Patch, "/api/settings", R"({"log_level":9})");
    expect(oob.status == 400 && oob.body.find("INVALID_FIELD") != std::string::npos, "log_level 9 out of range 400");

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
    expect(Json::parse(g3.body)["log_level"].as<int64_t>() == 2, "failed patch left log_level unchanged");

    // Malformed JSON body → 400 INVALID_BODY (Router maps JsonException → 400).
    auto mal = app.call(Method::Patch, "/api/settings", "{not json");
    expect(mal.status == 400 && mal.body.find("code") != std::string::npos, "malformed JSON body 400");

    // Non-object body → 400 INVALID_FIELD.
    auto arr = app.call(Method::Patch, "/api/settings", "[1,2]");
    expect(arr.status == 400 && arr.body.find("INVALID_FIELD") != std::string::npos, "non-object body 400 INVALID_FIELD");

    // ── §12.1: log_console / log_console_level round-trip + unknown-field ignore ──

    // PATCH both console settings → 200, GET reflects them.
    auto sc = app.call(Method::Patch, "/api/settings", R"({"log_console":true,"log_console_level":2})");
    expect(sc.status == 200, "patch log_console/log_console_level 200");
    auto scg = app.call(Method::Get, "/api/settings");
    auto scj = Json::parse(scg.body);
    expect(scj["log_console"].as<bool>() == true, "log_console reflected by GET");
    expect(scj["log_console_level"].as<int64_t>() == 2, "log_console_level reflected by GET");

    // Wrong type for log_console_level → 400 INVALID_FIELD (same guard as log_level).
    auto scb = app.call(Method::Patch, "/api/settings", R"({"log_console_level":"x"})");
    expect(scb.status == 400 && scb.body.find("INVALID_FIELD") != std::string::npos, "log_console_level bad type 400");

    // Unknown extra field → 200 (ignored, not rejected).
    auto scx = app.call(Method::Patch, "/api/settings", R"({"unknown_extra_field":42})");
    expect(scx.status == 200, "unknown extra field ignored 200");

    // Round-trip the other way (true → false) then restore to the benign default.
    auto scf = app.call(Method::Patch, "/api/settings", R"({"log_console":false})");
    expect(scf.status == 200, "patch log_console false 200");
    auto scg2 = app.call(Method::Get, "/api/settings");
    auto scj2 = Json::parse(scg2.body);
    expect(scj2["log_console"].as<bool>() == false, "log_console false reflected by GET");
    (void)app.call(Method::Patch, "/api/settings", R"({"log_console":true})");

    // ── C2: log_retention editable round-trip ──

    // PATCH log_retention → 200, GET reflects it.
    auto lr = app.call(Method::Patch, "/api/settings", R"({"log_retention":7})");
    expect(lr.status == 200, "patch log_retention 200");
    auto lrg = app.call(Method::Get, "/api/settings");
    auto lrj = Json::parse(lrg.body);
    expect(lrj.has("log_retention") && lrj["log_retention"].as<int64_t>() == 7, "log_retention reflected by GET");

    // Negative retention → 400 INVALID_FIELD (must not wrap into size_t).
    auto lrb = app.call(Method::Patch, "/api/settings", R"({"log_retention":-1})");
    expect(lrb.status == 400 && lrb.body.find("INVALID_FIELD") != std::string::npos, "negative log_retention 400");

    // Non-numeric retention → 400 INVALID_FIELD (JsonException → 400).
    auto lrt = app.call(Method::Patch, "/api/settings", R"({"log_retention":"x"})");
    expect(lrt.status == 400 && lrt.body.find("INVALID_FIELD") != std::string::npos, "log_retention bad type 400");
    (void)app.call(Method::Patch, "/api/settings", R"({"log_retention":5})"); // restore the default

    // ── C2: GET read-only path fields (set at startup, never writable) ──
    auto pth = app.call(Method::Get, "/api/settings");
    auto pj = Json::parse(pth.body);
    expect(pj.has("data_dir") && pj["data_dir"].type() == JsonType::String, "GET carries data_dir");
    expect(pj.has("log_dir") && pj["log_dir"].type() == JsonType::String, "GET carries log_dir");
    expect(pj.has("algo_dir") && pj["algo_dir"].type() == JsonType::String, "GET carries algo_dir");

    // ── C2: gui_port semantics without server injection ──
    // TestApp drives the Router directly (no WebModule, no real server): the
    // effective port is never injected, so gui_port must stay the configured
    // value. The injected-override path is covered in test_web_module (which
    // builds the real WebModule wiring).
    expect(pj.has("gui_port") && pj["gui_port"].as<int64_t>() == static_cast<int64_t>(AppConfig::get().gui_port),
           "gui_port stays the configured value without server injection");

    // ── GET read-only service fields (batch D): startup-only info is exposed
    //    so the settings page can display it ──
    auto sg = app.call(Method::Get, "/api/settings");
    auto sgj = Json::parse(sg.body);
    expect(sgj.has("gui_workers") && sgj["gui_workers"].as<int64_t>() >= 1, "GET carries gui_workers");
    expect(sgj.has("memory_mb") && sgj["memory_mb"].as<int64_t>() > 0, "GET carries memory_mb");
    expect(sgj.has("sandbox_enabled") && sgj["sandbox_enabled"].type() == JsonType::Bool, "GET carries sandbox_enabled");

    // ── config.json persistence (batch D): a successful PATCH writes the
    //    five runtime fields to <cwd>/config.json (best-effort) ──
    const std::string cfg_path = "config.json";
    std::error_code ec;
    std::filesystem::remove(cfg_path, ec); // stale file from an earlier run
    auto persist = app.call(Method::Patch, "/api/settings", R"({"log_level":1,"log_console":false,"log_console_level":3})");
    expect(persist.status == 200, "persistence patch 200");
    expect(std::filesystem::exists(cfg_path), "PATCH wrote config.json");
    bool cfg_shape = false, cfg_lang = false;
    bool cfg_lv = false, cfg_cc = false, cfg_cl = false, cfg_ret = false;
    if (std::filesystem::exists(cfg_path)) {
        std::ifstream in(cfg_path);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto cj = Json::parse(content);
        cfg_shape = cj.has("lang") && cj.has("log_level") && cj.has("log_console") && cj.has("log_console_level") &&
                    cj.has("log_retention");
        // lang is whatever the LanguageManager currently has active (no
        // translations are registered in the test process, so the name is
        // "" there) — assert the field exists as a string, not its value.
        cfg_lang = cj["lang"].type() == JsonType::String;
        cfg_lv = cj["log_level"].as<int64_t>() == 1;
        cfg_cc = cj["log_console"].as<bool>() == false;
        cfg_cl = cj["log_console_level"].as<int64_t>() == 3;
        // log_retention was restored to 5 right above the persistence PATCH.
        cfg_ret = cj["log_retention"].as<int64_t>() == 5;
    }
    expect(cfg_shape, "config.json carries all 5 runtime fields");
    expect(cfg_lang, "config.json carries a non-empty lang");
    expect(cfg_lv, "config.json log_level persisted as 1");
    expect(cfg_cc, "config.json log_console persisted as false");
    expect(cfg_cl, "config.json log_console_level persisted as 3");
    expect(cfg_ret, "config.json log_retention persisted as 5");
    // Restore the benign state the earlier cases established (the restore
    // PATCH rewrites config.json), then drop the file.
    (void)app.call(Method::Patch, "/api/settings", R"({"log_level":2,"log_console":true,"log_console_level":2})");
    std::filesystem::remove(cfg_path, ec);
}

void test_profiles(TestApp& app) {
    std::string key = "builtin:vanilla"; // the guaranteed root profile
    expect(app.ctx.profile_exists(key), "root profile present");

    // ── 1. list → 200 with "profiles" and "active" ──
    auto l = app.call(Method::Get, "/api/profiles");
    expect(l.status == 200 && l.body.find("profiles") != std::string::npos && l.body.find("active") != std::string::npos,
           "profiles list 200 + fields");

    // ── 2. read metadata → every ProfileMeta field present ──
    auto r = app.call(Method::Get, "/api/profiles/" + key);
    expect(r.status == 200, "profile metadata 200");
    for (const char* f : {"name", "dependencies", "is_root", "format", "ench_count", "eq_count", "tag_count", "version",
                          "release_tag", "description", "mc_version"})
        expect(r.body.find(f) != std::string::npos, std::string("metadata field ") + f);
    // C4: the builtin root must expose restored description + mc_version.
    auto rj = Json::parse(r.body);
    expect(rj.has("description") && !rj["description"].as<std::string>().empty(), "metadata description present and non-empty");
    expect(rj.has("mc_version") && !rj["mc_version"].as<std::string>().empty(), "metadata mc_version present and non-empty");

    // ── 3. equipments round-trip: delete-existing → add → read → update → read → delete ──
    // `minecraft:netherite_sword` ships in the builtin profile, so the "add"
    // (which must be 201, not 409) needs the pre-existing entry removed first.
    auto pre = app.call(Method::Delete, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(pre.status == 204, "equip pre-delete existing 204");

    auto add = app.call(Method::Post, "/api/profiles/" + key + "/equipments",
                        R"({"id":"minecraft:netherite_sword","max_durability":2031})");
    expect(add.status == 201, "equip add 201");
    expect(add.header_value("Location").find("minecraft:netherite_sword") != std::string::npos, "equip add Location header");

    auto lst = app.call(Method::Get, "/api/profiles/" + key + "/equipments");
    expect(lst.status == 200 && lst.body.find("netherite_sword") != std::string::npos, "equip list contains added entry");

    auto upd = app.call(Method::Patch, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword",
                        R"({"id":"minecraft:netherite_sword","max_durability":5000})");
    expect(upd.status == 200, "equip update 200");

    auto rd = app.call(Method::Get, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(rd.status == 200 && rd.body.find("5000") != std::string::npos, "equip read reflects updated durability");

    auto del = app.call(Method::Delete, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(del.status == 204, "equip delete 204");
    auto gone = app.call(Method::Get, "/api/profiles/" + key + "/equipments/minecraft:netherite_sword");
    expect(gone.status == 404, "equip read after delete 404");

    // ── 4. errors ──
    auto nope = app.call(Method::Get, "/api/profiles/nope");
    expect(nope.status == 404, "unknown profile 404");
    auto noench = app.call(Method::Get, "/api/profiles/" + key + "/enchantments/nope");
    expect(noench.status == 404, "unknown enchantment 404");
    auto dup = app.call(Method::Post, "/api/profiles", R"({"source":")" + key + R"(","dest":")" + key + R"("})");
    expect(dup.status == 409, "create existing dest 409");

    // Empty dest in create/fork → 400 (was a 500 before the guard).
    auto emptyd = app.call(Method::Post, "/api/profiles", R"({"source":")" + key + R"(","dest":""})");
    expect(emptyd.status == 400 && emptyd.body.find("INVALID_FIELD") != std::string::npos,
           "create empty dest 400 INVALID_FIELD");
    auto emptyf = app.call(Method::Post, "/api/profiles/" + key + "/fork", R"({"dest":""})");
    expect(emptyf.status == 400 && emptyf.body.find("INVALID_FIELD") != std::string::npos, "fork empty dest 400 INVALID_FIELD");

    // PATCH sub-resource: path segment must match the body id (was a silent 200).
    auto mism = app.call(Method::Patch, "/api/profiles/" + key + "/equipments/minecraft:iron_sword",
                         R"({"id":"minecraft:netherite_sword","max_durability":5000})");
    expect(mism.status == 400 && mism.body.find("INVALID_FIELD") != std::string::npos,
           "PATCH path/body id mismatch 400 INVALID_FIELD");
    auto bad = app.call(Method::Patch, "/api/profiles/" + key, R"({"dependencies":"x"})");
    expect(bad.status == 400, "patch bad dependencies 400");
    auto badobj = app.call(Method::Patch, "/api/profiles/" + key, "[1,2]");
    expect(badobj.status == 400, "patch non-object body 400");

    // ── 5. rename (on a fork, so the original key survives for later) ──
    auto fr = app.call(Method::Post, "/api/profiles", R"({"source":")" + key + R"(","dest":")" + key + R"(-rs"})");
    expect(fr.status == 201, "fork for rename");
    auto rn = app.call(Method::Post, "/api/profiles/" + key + "-rs/rename", R"({"name":")" + key + R"(-rd"})");
    expect(rn.status == 200, "rename 200");
    auto rr = app.call(Method::Get, "/api/profiles/" + key + "-rd");
    expect(rr.status == 200, "renamed profile readable");

    // ── 6. dependency update (on a scratch fork, so the root survives) ──
    auto fd = app.call(Method::Post, "/api/profiles", R"({"source":")" + key + R"(","dest":")" + key + R"(-deps"})");
    expect(fd.status == 201, "fork for dependencies");

    // Empty dependency list → 200, then readback shows an empty array.
    auto pd = app.call(Method::Patch, "/api/profiles/" + key + "-deps", R"({"dependencies":[]})");
    expect(pd.status == 200, "patch dependencies [] 200");
    auto gd = app.call(Method::Get, "/api/profiles/" + key + "-deps");
    expect(gd.status == 200, "deps profile readable");
    auto gd_json = Json::parse(gd.body);
    expect(gd_json["dependencies"].type() == JsonType::Array && gd_json["dependencies"].as_array().empty(),
           "dependencies empty after [] patch");

    // Set an actual dependency on an existing profile; readback contains it.
    auto ps = app.call(Method::Patch, "/api/profiles/" + key + "-deps", R"({"dependencies":[")" + key + R"("]})");
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

/// §12.1 matrix rows not covered by test_profiles: the profile action endpoints
/// (activate/fork/merge/publish/rename), DELETE 204+disappears, create error
/// paths (unknown source / empty body), PATCH cycle → 409, and the full tags
/// + enchantments sub-resource round-trips. Runs AFTER test_profiles so the
/// renamed profile `builtin:vanilla-rd` exists (rename-conflict target).
void test_profile_actions(TestApp& app) {
    const std::string key = "builtin:vanilla";

    // ── POST /api/profiles error paths ──
    auto miss_src = app.call(Method::Post, "/api/profiles", R"({"source":"nope","dest":"builtin:vanilla-nope1"})");
    expect(miss_src.status == 404 && miss_src.body.find("PROFILE_NOT_FOUND") != std::string::npos,
           "create with unknown source 404 PROFILE_NOT_FOUND");
    auto empty_body = app.call(Method::Post, "/api/profiles", "{}");
    expect(empty_body.status == 400 && empty_body.body.find("code") != std::string::npos,
           "create with {} body (no source/dest) 400");

    // Scaffold forks (all from the root; DELETE/activate/merge/publish/fork/cycle).
    auto scaffold = [&](const char* dest) {
        auto r =
            app.call(Method::Post, "/api/profiles", std::string(R"({"source":")") + key + R"(","dest":")" + dest + R"("})");
        expect(r.status == 201, std::string("fork scaffold ") + dest);
    };

    // ── DELETE /api/profiles/{key} → 204, then GET → 404 ──
    scaffold("builtin:vanilla-del");
    auto del_r = app.call(Method::Delete, "/api/profiles/builtin:vanilla-del");
    expect(del_r.status == 204, "delete profile 204");
    auto del_g = app.call(Method::Get, "/api/profiles/builtin:vanilla-del");
    expect(del_g.status == 404 && del_g.body.find("PROFILE_NOT_FOUND") != std::string::npos, "deleted profile read 404");

    // ── DELETE the root profile → 409 PROFILE_IS_ROOT, and it survives ──
    // (the builtin base is the implicit lowest-priority source of every
    // effective view, so removing it must be impossible through the API).
    auto del_root = app.call(Method::Delete, "/api/profiles/builtin:vanilla");
    expect(del_root.status == 409 && del_root.body.find("PROFILE_IS_ROOT") != std::string::npos,
           "delete root profile 409 PROFILE_IS_ROOT");
    auto root_alive = app.call(Method::Get, "/api/profiles/builtin:vanilla");
    expect(root_alive.status == 200, "root profile survives delete attempt");

    // ── POST /api/profiles/{key}/activate → 200 {ok:true}, takes effect, back ──
    scaffold("builtin:vanilla-act");
    auto act = app.call(Method::Post, "/api/profiles/builtin:vanilla-act/activate");
    expect(act.status == 200 && act.body.find("\"ok\":true") != std::string::npos, "activate 200 ok:true");
    auto st = app.call(Method::Get, "/api/status");
    expect(Json::parse(st.body)["active_profile"].as<std::string>() == "builtin:vanilla-act",
           "activate took effect (status active_profile)");
    auto act_back = app.call(Method::Post, "/api/profiles/builtin:vanilla/activate");
    expect(act_back.status == 200, "reactivate root 200");
    auto st2 = app.call(Method::Get, "/api/status");
    expect(Json::parse(st2.body)["active_profile"].as<std::string>() == "builtin:vanilla",
           "root reactivated (active_profile restored)");
    auto act_nope = app.call(Method::Post, "/api/profiles/nope/activate");
    expect(act_nope.status == 404, "activate unknown profile 404");

    // ── POST /api/profiles/{key}/merge → 200; unknown source/dest → 404 ──
    scaffold("builtin:vanilla-mg1");
    scaffold("builtin:vanilla-mg2");
    auto mg = app.call(Method::Post, "/api/profiles/builtin:vanilla-mg1/merge", R"({"dest":"builtin:vanilla-mg2"})");
    expect(mg.status == 200 && mg.body.find("\"ok\":true") != std::string::npos, "merge 200 ok:true");
    auto mg_src = app.call(Method::Post, "/api/profiles/nope/merge", R"({"dest":"builtin:vanilla-mg2"})");
    expect(mg_src.status == 404 && mg_src.body.find("PROFILE_NOT_FOUND") != std::string::npos, "merge unknown source 404");
    auto mg_dst = app.call(Method::Post, "/api/profiles/builtin:vanilla-mg1/merge", R"({"dest":"nope"})");
    expect(mg_dst.status == 404 && mg_dst.body.find("PROFILE_NOT_FOUND") != std::string::npos, "merge unknown dest 404");

    // ── POST /api/profiles/{key}/publish → 200 + file on disk (version/tag) ──
    scaffold("builtin:vanilla-pub");
    const std::string pub_path = "besq_web_test_publish.json";
    std::error_code ec;
    std::filesystem::remove(pub_path, ec); // stale file from a crashed earlier run
    auto pub = app.call(Method::Post, "/api/profiles/builtin:vanilla-pub/publish",
                        R"({"version":"1.2.3","tag":"rel-test","path":")" + pub_path + R"("})");
    expect(pub.status == 200 && pub.body.find("\"ok\":true") != std::string::npos, "publish 200 ok:true");
    expect(std::filesystem::exists(pub_path), "publish wrote the profile file");
    {
        std::ifstream in(pub_path);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        expect(content.find("1.2.3") != std::string::npos, "published file embeds version");
        expect(content.find("rel-test") != std::string::npos, "published file embeds release tag");
    }
    std::filesystem::remove(pub_path, ec);
    // Write failure (parent dir does not exist) → 400 PUBLISH_FAILED.
    auto pub_bad =
        app.call(Method::Post, "/api/profiles/builtin:vanilla-pub/publish", R"({"path":"no_such_dir_xyz/publish.json"})");
    expect(pub_bad.status == 400 && pub_bad.body.find("PUBLISH_FAILED") != std::string::npos,
           "publish to unwritable path 400 PUBLISH_FAILED");

    // ── POST /api/profiles/{key}/fork → 201 + Location; dest exists → 409 ──
    auto fk = app.call(Method::Post, "/api/profiles/builtin:vanilla/fork", R"({"dest":"builtin:vanilla-fk1"})");
    expect(fk.status == 201, "fork action 201");
    expect(fk.header_value("Location").find("/api/profiles/builtin:vanilla-fk1") != std::string::npos,
           "fork action Location header");
    auto fk2 = app.call(Method::Post, "/api/profiles/builtin:vanilla/fork", R"({"dest":"builtin:vanilla-fk1"})");
    expect(fk2.status == 409 && fk2.body.find("PROFILE_EXISTS") != std::string::npos, "fork dest exists 409 PROFILE_EXISTS");

    // ── POST /api/profiles/{key}/rename → target already exists → 409 ──
    // (`builtin:vanilla-rd` was created by test_profiles' rename round-trip.)
    auto rn = app.call(Method::Post, "/api/profiles/builtin:vanilla-fk1/rename", R"({"name":"builtin:vanilla-rd"})");
    expect(rn.status == 409 && rn.body.find("PROFILE_EXISTS") != std::string::npos,
           "rename to existing target 409 PROFILE_EXISTS");

    // ── PATCH /api/profiles/{key} errors: unknown key → 404; cycle → 409 ──
    auto pu = app.call(Method::Patch, "/api/profiles/nope", R"({"dependencies":[]})");
    expect(pu.status == 404 && pu.body.find("PROFILE_NOT_FOUND") != std::string::npos, "patch unknown profile 404");
    scaffold("builtin:vanilla-cya");
    scaffold("builtin:vanilla-cyb");
    auto cy1 = app.call(Method::Patch, "/api/profiles/builtin:vanilla-cya", R"({"dependencies":["builtin:vanilla-cyb"]})");
    expect(cy1.status == 200, "set dependency a→b 200");
    auto cy2 = app.call(Method::Patch, "/api/profiles/builtin:vanilla-cyb", R"({"dependencies":["builtin:vanilla-cya"]})");
    expect(cy2.status == 409 && cy2.body.find("DEPENDENCY_CYCLE") != std::string::npos,
           "dependency cycle 409 DEPENDENCY_CYCLE");

    // ── tags sub-resource: full round-trip + duplicate → 409 ──
    // Tag ids are `#`-prefixed NSIDs; the test Router dispatches the raw path,
    // so the `#` inside the path segment is handled by match_segments verbatim.
    const std::string tag_path = key + "/tags/#minecraft:test_tag";
    auto tag_pre = app.call(Method::Delete, "/api/profiles/" + tag_path);
    expect(tag_pre.status == 204 || tag_pre.status == 404, "tag pre-delete tolerated");
    auto tag_add =
        app.call(Method::Post, "/api/profiles/" + key + "/tags", R"({"id":"#minecraft:test_tag","name":"Test Tag"})");
    expect(tag_add.status == 201, "tag add 201");
    expect(tag_add.header_value("Location").find("#minecraft:test_tag") != std::string::npos, "tag add Location header");
    auto tag_dup =
        app.call(Method::Post, "/api/profiles/" + key + "/tags", R"({"id":"#minecraft:test_tag","name":"Test Tag"})");
    expect(tag_dup.status == 409 && tag_dup.body.find("DUPLICATE_ENTRY") != std::string::npos,
           "tag duplicate add 409 DUPLICATE_ENTRY");
    auto tag_list = app.call(Method::Get, "/api/profiles/" + key + "/tags");
    expect(tag_list.status == 200 && tag_list.body.find("test_tag") != std::string::npos, "tag list contains added tag");
    auto tag_read = app.call(Method::Get, "/api/profiles/" + tag_path);
    expect(tag_read.status == 200 && tag_read.body.find("Test Tag") != std::string::npos, "tag read 200 + name field");
    auto tag_patch =
        app.call(Method::Patch, "/api/profiles/" + tag_path, R"({"id":"#minecraft:test_tag","name":"Test Tag V2"})");
    expect(tag_patch.status == 200, "tag patch 200");
    auto tag_read2 = app.call(Method::Get, "/api/profiles/" + tag_path);
    expect(tag_read2.status == 200 && tag_read2.body.find("Test Tag V2") != std::string::npos,
           "tag read reflects patched name");
    auto tag_del = app.call(Method::Delete, "/api/profiles/" + tag_path);
    expect(tag_del.status == 204, "tag delete 204");
    auto tag_gone = app.call(Method::Get, "/api/profiles/" + tag_path);
    expect(tag_gone.status == 404, "tag read after delete 404");

    // ── enchantments sub-resource: success round-trip on a scratch id ──
    // (error paths — unknown name 404 — are already covered in test_profiles.)
    const std::string ench_path = key + "/enchantments/test:e_matrix_ench";
    auto ench_pre = app.call(Method::Delete, "/api/profiles/" + ench_path);
    expect(ench_pre.status == 204 || ench_pre.status == 404, "ench pre-delete tolerated");
    const std::string ench_body = R"({"id":"test:e_matrix_ench","name":"Matrix Ench","max_level":5,)"
                                  R"("multiplier":1,"supported_items":["#minecraft:swords"]})";
    auto ench_add = app.call(Method::Post, "/api/profiles/" + key + "/enchantments", ench_body);
    expect(ench_add.status == 201, "ench add 201");
    expect(ench_add.header_value("Location").find("test:e_matrix_ench") != std::string::npos, "ench add Location header");
    auto ench_dup = app.call(Method::Post, "/api/profiles/" + key + "/enchantments", ench_body);
    expect(ench_dup.status == 409 && ench_dup.body.find("DUPLICATE_ENTRY") != std::string::npos,
           "ench duplicate add 409 DUPLICATE_ENTRY");
    auto ench_list = app.call(Method::Get, "/api/profiles/" + key + "/enchantments");
    expect(ench_list.status == 200 && ench_list.body.find("e_matrix_ench") != std::string::npos,
           "ench list contains added entry");
    auto ench_patch = app.call(Method::Patch, "/api/profiles/" + ench_path,
                               R"({"id":"test:e_matrix_ench","name":"Matrix Ench V2",)"
                               R"("max_level":7,"multiplier":1,"supported_items":["#minecraft:swords"]})");
    expect(ench_patch.status == 200, "ench patch 200");
    auto ench_read = app.call(Method::Get, "/api/profiles/" + ench_path);
    expect(ench_read.status == 200 && ench_read.body.find("Matrix Ench V2") != std::string::npos &&
               ench_read.body.find("\"max_level\":7") != std::string::npos,
           "ench read reflects patched name + max_level");
    auto ench_del = app.call(Method::Delete, "/api/profiles/" + ench_path);
    expect(ench_del.status == 204, "ench delete 204");
    auto ench_gone = app.call(Method::Get, "/api/profiles/" + ench_path);
    expect(ench_gone.status == 404, "ench read after delete 404");
}

/// §12.1: GET /api/profiles/{key}/enchantables/{item} — enchantments
/// applicable to an item (effective-view tag resolution + platform gate,
/// mirroring solve's CompactAdapter::apply).
void test_enchantables(TestApp& app) {
    const std::string key = "builtin:vanilla";

    // ── 1. diamond_sword → 200: sharpness (+ full field shape) present,
    //        efficiency/protection (wrong item category) absent ──
    auto sw = app.call(Method::Get, "/api/profiles/" + key + "/enchantables/minecraft:diamond_sword");
    expect(sw.status == 200, "diamond_sword enchantables 200");
    for (const char* f :
         {"\"id\":", "\"name\":", "\"max_level\":", "\"is_treasure\":", "\"exclusive_set\":", "\"multiplier\":"})
        expect(sw.body.find(f) != std::string::npos, std::string("enchantables field ") + f);
    expect(sw.body.find("minecraft:sharpness") != std::string::npos, "sharpness applicable to diamond_sword");
    expect(sw.body.find("\"max_level\":5") != std::string::npos, "sharpness max_level 5 present");
    expect(sw.body.find("\"exclusive_set\":[") != std::string::npos && sw.body.find("minecraft:smite") != std::string::npos,
           "exclusive_set array contains smite");
    expect(sw.body.find("minecraft:efficiency") == std::string::npos, "efficiency NOT applicable to diamond_sword");
    expect(sw.body.find("minecraft:protection") == std::string::npos, "protection NOT applicable to diamond_sword");

    // ── 2. enchanted_book → 200 full registry (every enchantment) ──
    auto bk = app.call(Method::Get, "/api/profiles/" + key + "/enchantables/minecraft:enchanted_book");
    expect(bk.status == 200, "enchanted_book enchantables 200");
    expect(bk.body.find("minecraft:sharpness") != std::string::npos &&
               bk.body.find("minecraft:efficiency") != std::string::npos,
           "book returns the full registry (sharpness + efficiency)");

    // ── 3. errors: unknown profile / unknown item / invalid NSID → 404 ──
    auto noprof = app.call(Method::Get, "/api/profiles/nope/enchantables/minecraft:diamond_sword");
    expect(noprof.status == 404 && noprof.body.find("PROFILE_NOT_FOUND") != std::string::npos,
           "unknown profile 404 PROFILE_NOT_FOUND");
    auto noitem = app.call(Method::Get, "/api/profiles/" + key + "/enchantables/minecraft:no_such_sword");
    expect(noitem.status == 404 && noitem.body.find("ENTRY_NOT_FOUND") != std::string::npos,
           "unknown item 404 ENTRY_NOT_FOUND");
    auto badid = app.call(Method::Get, "/api/profiles/" + key + "/enchantables/bad item");
    expect(badid.status == 404 && badid.body.find("ENTRY_NOT_FOUND") != std::string::npos,
           "invalid NSID path segment 404 ENTRY_NOT_FOUND");

    // ── 4. scaffold fork: tag membership / platform gate / nested-tag BFS ──
    auto sc = app.call(Method::Post, "/api/profiles", R"({"source":")" + key + R"(","dest":")" + key + R"(-ench"})");
    expect(sc.status == 201, "fork scaffold for enchantables");
    const std::string skey = key + "-ench";
    auto add_ench = [&](const std::string& id, const char* platform, const char* supported) {
        const std::string body = R"({"id":")" + id + R"(","name":")" + id + R"(","platform":")" + platform +
                                 R"(","max_level":3,"multiplier":2,"supported_items":[")" + supported + R"("]})";
        auto r = app.call(Method::Post, "/api/profiles/" + skey + "/enchantments", body);
        expect(r.status == 201, std::string("scaffold ench ") + id);
    };
    add_ench("test:sword_hit", "java", "#minecraft:swords");
    add_ench("test:bedrock_only", "bedrock", "#minecraft:swords");
    add_ench("test:nested_hit", "java", "#minecraft:enchantable/weapon");

    auto sh = app.call(Method::Get, "/api/profiles/" + skey + "/enchantables/minecraft:diamond_sword");
    expect(sh.status == 200, "scaffold diamond_sword enchantables 200");
    expect(sh.body.find("test:sword_hit") != std::string::npos, "custom tag #minecraft:swords membership hits");
    expect(sh.body.find("test:nested_hit") != std::string::npos, "nested tag #minecraft:enchantable/weapon hits via BFS");
    expect(sh.body.find("test:bedrock_only") == std::string::npos, "bedrock-only enchantment filtered by platform gate");
}

/// GET /api/fs/list?path= — directory listing for the picker. Root-locked to
/// the server cwd (= PROJECT_ROOT in the test harness): a valid directory
/// lists; a file, a missing path, or an escape above the root → 400.
void test_fs(TestApp& app) {
    // ── 1. root (empty path) → 200 with path/root/entries ──
    auto root = app.call(Method::Get, "/api/fs/list");
    expect(root.status == 200, "fs list root 200");
    auto rj = Json::parse(root.body);
    expect(rj["path"].type() == JsonType::String && rj["root"].type() == JsonType::String, "fs list carries path + root");
    expect(rj["entries"].type() == JsonType::Array && !rj["entries"].as_array().empty(),
           "fs root entries non-empty (cwd = project root)");
    expect(rj["path"].as<std::string>() == rj["root"].as<std::string>(), "empty path resolves to the root itself");

    // ── 2. a known subdirectory (`data` ships in the repo) → 200, dirs-first ──
    auto data = app.call(Method::Get, "/api/fs/list?path=data");
    expect(data.status == 200, "fs list data 200");
    auto dj = Json::parse(data.body);
    expect(dj["entries"].type() == JsonType::Array && !dj["entries"].as_array().empty(), "data entries non-empty");
    // Directory entries come first and carry is_dir/size fields.
    bool first_is_dir = dj["entries"].as_array().front()["is_dir"].as<bool>();
    expect(first_is_dir, "entries sorted directories-first");
    expect(dj["entries"].as_array().front().has("name") && dj["entries"].as_array().front().has("size"),
           "entry carries name + size");
    expect(dj["path"].as<std::string>().find("data") != std::string::npos, "data path reflected in response");

    // ── 3. invalid paths → 400 INVALID_PATH ──
    auto file = app.call(Method::Get, "/api/fs/list?path=CMakeLists.txt");
    expect(file.status == 400 && file.body.find("INVALID_PATH") != std::string::npos, "fs list on a file 400 INVALID_PATH");
    auto miss = app.call(Method::Get, "/api/fs/list?path=data%2Fno_such_dir_xyz");
    expect(miss.status == 400 && miss.body.find("INVALID_PATH") != std::string::npos,
           "fs list on a missing dir 400 INVALID_PATH");
    auto esc = app.call(Method::Get, "/api/fs/list?path=..%2F..%2F..%2F..%2F..%2F..%2F");
    expect(esc.status == 400 && esc.body.find("INVALID_PATH") != std::string::npos,
           "fs list escaping the root 400 INVALID_PATH");
    auto rel_esc = app.call(Method::Get, "/api/fs/list?path=data%2F..%2F..%2F");
    expect(rel_esc.status == 400 && rel_esc.body.find("INVALID_PATH") != std::string::npos,
           "fs list ..-escape via relative path 400 INVALID_PATH");
}

void test_algorithms(TestApp& app) {
    // list → 200 array of names containing a builtin strategy.
    auto l = app.call(Method::Get, "/api/algorithms");
    expect(l.status == 200 && l.body.find("dp_merge") != std::string::npos, "algorithms list 200 contains dp_merge");

    // detail → every AlgorithmDetail field serialized.
    auto d = app.call(Method::Get, "/api/algorithms/dp_merge");
    expect(d.status == 200, "algorithm detail 200");
    for (const char* f :
         {"name", "version", "origin", "supported_mode", "is_resumable", "plugin_path", "has_audit", "evaluate"})
        expect(d.body.find(f) != std::string::npos, std::string("detail field ") + f);

    // evaluate → predicted seconds for N=16: dp_merge is a search DP (> 0);
    // hamming is deterministic (exactly 0 — a valid number, not "absent").
    auto dj = Json::parse(d.body);
    expect(dj.has("evaluate") && dj["evaluate"].type() == JsonType::Number, "dp_merge evaluate present as number");
    expect(dj["evaluate"].as_double() > 0.0, "dp_merge evaluate(16) > 0");
    auto h = app.call(Method::Get, "/api/algorithms/hamming");
    auto hj = Json::parse(h.body);
    expect(h.status == 200 && hj.has("evaluate") && hj["evaluate"].type() == JsonType::Number &&
               hj["evaluate"].as_double() == 0.0,
           "hamming evaluate present and 0 (deterministic)");

    // Unloading a builtin (trusted kernel) is rejected → 400 UNLOAD_REJECTED.
    // No solve is active at this point, so the gate (409 TASK_ACTIVE) is clear.
    auto un = app.call(Method::Post, "/api/algorithms/unload", R"({"name":"dp_merge"})");
    expect(un.status == 400 && un.body.find("UNLOAD_REJECTED") != std::string::npos, "unload builtin 400 UNLOAD_REJECTED");

    // Unknown algorithm → 404.
    auto no = app.call(Method::Get, "/api/algorithms/nope");
    expect(no.status == 404, "unknown algorithm 404");

    // Load with a missing/invalid body → 400 INVALID_FIELD (code present).
    auto load_empty = app.call(Method::Post, "/api/algorithms/load", "{}");
    expect(load_empty.status == 400 && load_empty.body.find("code") != std::string::npos, "algorithms load {} 400 with code");

    // Load from a nonexistent directory → 200 {"loaded":0} (scan does not throw).
    auto load_missing = app.call(Method::Post, "/api/algorithms/load", R"({"dir":"/nonexistent_dir_xyz"})");
    expect(load_missing.status == 200, "algorithms load missing dir 200");
    auto lj = Json::parse(load_missing.body);
    expect(lj["loaded"].as<int64_t>() == 0, "algorithms load missing dir loaded=0");
}

void test_calculator(TestApp& app) {
    // Light target → 202 + task_id + Location.
    auto light = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(light.status == 202, "light task submit 202");
    auto lb = Json::parse(light.body);
    expect(lb["task_id"].type() == JsonType::String, "light task_id returned");
    expect(light.header_value("Location").find("/api/tasks/") != std::string::npos, "submit Location header");
    std::string light_id = lb["task_id"].as<std::string>();

    // Poll the light task to a terminal state (bounded ≤5s) so cancel/status on
    // a completed task below are deterministic.
    bool light_done = false;
    for (int i = 0; i < 50 && !light_done; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + light_id);
        if (st.status != 200)
            break;
        auto sj = Json::parse(st.body);
        light_done = sj["state"].as<std::string>() != "running";
        if (!light_done)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(light_done, "light task reached terminal state");

    // DELETE unknown → 404; DELETE a completed task → 200 no-op.
    auto cno = app.call(Method::Delete, "/api/tasks/nope");
    expect(cno.status == 404 && cno.body.find("code") != std::string::npos, "cancel unknown task 404");
    auto cdone = app.call(Method::Delete, "/api/tasks/" + light_id);
    expect(cdone.status == 200 && cdone.body.find("ok") != std::string::npos, "cancel completed task 200 no-op");

    // Status of the completed light task → 200 with a result payload.
    auto sdone = app.call(Method::Get, "/api/tasks/" + light_id);
    expect(sdone.status == 200 && sdone.body.find("result") != std::string::npos, "status completed task carries result");

    // ── §12.1: a deterministically failing task → snapshot state=failed + error ──
    // Unknown enchantment id throws inside the worker's build_request (before
    // solve), so the failure is deterministic and fast. The snapshot does not
    // race: the task stays in the table until the next start() reaps it.
    // (Placed here, AFTER the light-task assertions, because the next submit
    // reaps the completed light task — the cancel-completed no-op above must
    // see it first.)
    auto fail = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"no_such_ench_xyz","level":1}]},
        "algorithm":"dp_merge"
    })");
    expect(fail.status == 202, "failing task submit 202");
    auto failb = Json::parse(fail.body);
    std::string fail_id = failb["task_id"].as<std::string>();
    bool fail_done = false;
    for (int i = 0; i < 50 && !fail_done; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + fail_id);
        if (st.status == 200) {
            auto sj = Json::parse(st.body);
            fail_done = sj["state"].as<std::string>() != "running";
        }
        if (!fail_done)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(fail_done, "failing task reached terminal state");
    auto fs = app.call(Method::Get, "/api/tasks/" + fail_id);
    expect(fs.status == 200 && fs.body.find("\"state\":\"failed\"") != std::string::npos, "failed snapshot state=failed");
    expect(fs.body.find("\"error\"") != std::string::npos && fs.body.find("\"progress\"") != std::string::npos,
           "failed snapshot carries error + progress fields");
    // SolveHistory（B2）：worker 终态 Failed 事件带 error_message（与状态字一致；
    // 状态可观察与事件记录间有微小窗口，故有界重试）。
    bool saw_failed = false, failed_err = false;
    for (int i = 0; i < 50 && !saw_failed; ++i) {
        for (const auto& ev : app.ctx.solve_history()) {
            if (ev.type != SolveEventType::Failed || ev.task_id != fail_id)
                continue;
            saw_failed = true;
            failed_err = !ev.error_message.empty();
        }
        if (!saw_failed)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(saw_failed, "failed web task records a Failed event");
    expect(failed_err, "Failed event carries error_message");

    // ── §12.1: task DTO full-field validation — every optional field accepted ──
    // One optional field per task (matrix row: 逐项); each is polled to a
    // terminal state so the single active slot is free for the next submit.
    const std::string dtomin = R"({"target":{"item":"diamond_sword","enchants":[)"
                               R"({"id":"sharpness","level":5}]},"algorithm":"dp_merge")";
    auto submit_ok = [&](const std::string& body, const char* label) {
        auto r = app.call(Method::Post, "/api/tasks", body);
        expect(r.status == 202, std::string("task field ") + label + " submit 202");
        std::string id;
        if (r.status == 202) {
            auto b = Json::parse(r.body);
            id = b["task_id"].as<std::string>();
        }
        bool done = false;
        for (int i = 0; i < 50 && !done; ++i) {
            auto st = app.call(Method::Get, "/api/tasks/" + id);
            if (st.status == 200) {
                auto sj = Json::parse(st.body);
                done = sj["state"].as<std::string>() != "running";
            }
            if (!done)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(done, std::string("task field ") + label + " reached terminal state");
    };
    submit_ok(dtomin + R"(,"max_solutions":2})", "max_solutions");
    submit_ok(dtomin + R"(,"max_search_time":5000})", "max_search_time");
    submit_ok(dtomin + R"(,"max_threads":2})", "max_threads");
    submit_ok(dtomin + R"(,"profile":"builtin:vanilla"})", "profile");
    submit_ok(dtomin + R"(,"source":[{"id":"sharpness","level":1}]})", "source");
    submit_ok(R"({"target":{"item":"diamond_sword","enchants":[)"
              R"({"id":"sharpness","level":5}]},"algorithm":"hamming",)"
              R"("items":[{"type":"book","enchants":[{"id":"sharpness","level":5}],"priority":1}]})",
              "items");
    // Wrong type for an optional field → 400 INVALID_TASK.
    auto badfield = app.call(Method::Post, "/api/tasks",
                             R"({"target":{"item":"diamond_sword","enchants":)"
                             R"([{"id":"sharpness","level":5}]},"max_threads":"x"})");
    expect(badfield.status == 400 && badfield.body.find("INVALID_TASK") != std::string::npos,
           "task max_threads wrong type 400 INVALID_TASK");

    // Submit with a missing required field ("target") → 400 INVALID_TASK.
    auto badtask = app.call(Method::Post, "/api/tasks", "{}");
    expect(badtask.status == 400 && badtask.body.find("code") != std::string::npos, "submit {} 400 with code");

    // test_profiles deletes netherite_sword from the builtin profile; restore it
    // so the heavy target resolves (idempotent: DELETE then POST).
    (void)app.call(Method::Delete, "/api/profiles/builtin:vanilla/equipments/minecraft:netherite_sword");
    auto readd = app.call(Method::Post, "/api/profiles/builtin:vanilla/equipments",
                          R"({"id":"minecraft:netherite_sword","max_durability":2031})");
    expect(readd.status == 201, "restore netherite_sword for heavy target");

    // Seed many custom sword enchantments so a dp_merge over all of them on a
    // netherite_sword stays Running long enough to observe the single-slot 409
    // (mirrors test_web_calculator::test_single_active_slot).
    for (int i = 0; i < 18; ++i) {
        EnchInfo info;
        info.id = NSID("test:e_" + std::to_string(i));
        info.name = "E " + std::to_string(i);
        info.max_level = 5;
        info.multiplier = 1;
        info.supported_items.insert(NSID("#minecraft:swords"));
        expect(app.ctx.add_enchantment(info), "seed ench " + std::to_string(i));
    }

    // ── §12.1: cancelled snapshot — heavy task cancelled immediately ──
    // The 18-ench dp_merge stays Running for ~a second (the same window the
    // single-slot 409 below relies on), so the DELETE fires while it is still
    // Running and the state is deterministically "cancelled".
    std::string heavy_cancel = R"({"target":{"item":"netherite_sword","enchants":[)";
    for (int i = 0; i < 18; ++i) {
        if (i)
            heavy_cancel += ",";
        heavy_cancel += R"({"id":"test:e_)" + std::to_string(i) + R"(","level":5})";
    }
    heavy_cancel += R"(]},"algorithm":"dp_merge"})";
    auto hc = app.call(Method::Post, "/api/tasks", heavy_cancel);
    expect(hc.status == 202, "cancel-test task submit 202");
    auto hcb = Json::parse(hc.body);
    std::string hc_id = hcb["task_id"].as<std::string>();
    auto hc_del = app.call(Method::Delete, "/api/tasks/" + hc_id);
    expect(hc_del.status == 200 && hc_del.body.find("ok") != std::string::npos, "cancel running task 200 ok");
    bool cancelled = false;
    for (int i = 0; i < 50 && !cancelled; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + hc_id);
        if (st.status == 200) {
            auto sj = Json::parse(st.body);
            cancelled = sj["state"].as<std::string>() == "cancelled";
        }
        if (!cancelled)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(cancelled, "cancelled snapshot state=cancelled");

    // 共享重任务（18-ench dp_merge 保持 Running ~1s）：pause/resume 测试与
    // 下面的 unload-409 单槽测试都用它。
    std::string heavy = R"({"target":{"item":"netherite_sword","enchants":[)";
    for (int i = 0; i < 18; ++i) {
        if (i)
            heavy += ",";
        heavy += R"({"id":"test:e_)" + std::to_string(i) + R"(","level":5})";
    }
    heavy += R"(]},"algorithm":"dp_merge"})";

    // ── SolveHistory web 记录点（计划 B Task B2）──
    // 1. Completed：轻任务终态事件带 task_id/target/algorithm/mode/成本字段。
    auto hl = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(hl.status == 202, "history task submit 202");
    std::string hl_id = Json::parse(hl.body)["task_id"].as<std::string>();
    bool hl_done = false;
    for (int i = 0; i < 50 && !hl_done; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + hl_id);
        if (st.status == 200)
            hl_done = Json::parse(st.body)["state"].as<std::string>() != "running";
        if (!hl_done)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(hl_done, "history task reached terminal state");
    bool saw_completed = false, completed_fields = false;
    for (int i = 0; i < 50 && !saw_completed; ++i) {
        for (const auto& ev : app.ctx.solve_history()) {
            if (ev.type != SolveEventType::Completed || ev.task_id != hl_id)
                continue;
            saw_completed = true;
            completed_fields = ev.target.find("diamond_sword[sharpness=5") != std::string::npos && ev.algorithm == "dp_merge" &&
                               ev.mode == "direct" && ev.total_level_cost > 0 && ev.solution_count > 0 &&
                               ev.computation_ms >= 0;
        }
        if (!saw_completed)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(saw_completed, "web task records a Completed event");
    expect(completed_fields, "Completed event carries task_id/target/algorithm/mode/costs");

    // 2. Cancelled：重任务提交后立即取消 → Cancelled 事件（worker 终态闸记录；
    // 与 heavy_cancel 同一确定性：18-ench dp_merge 提交后毫秒级不可能跑完）。
    auto hc2 = app.call(Method::Post, "/api/tasks", heavy);
    expect(hc2.status == 202, "history-cancel task submit 202");
    std::string hc2_id = Json::parse(hc2.body)["task_id"].as<std::string>();
    auto hc2_del = app.call(Method::Delete, "/api/tasks/" + hc2_id);
    expect(hc2_del.status == 200, "cancel history task 200");
    bool saw_cancelled = false;
    for (int i = 0; i < 50 && !saw_cancelled; ++i) {
        for (const auto& ev : app.ctx.solve_history())
            if (ev.type == SolveEventType::Cancelled && ev.task_id == hc2_id)
                saw_cancelled = true;
        if (!saw_cancelled)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(saw_cancelled, "cancelled web task records a Cancelled event");

    // ── Batch C: ignore_incompatible 接通 —— 冲突目标（sharpness+smite 同
    // target）默认严格冲突（completed + result.success:false），带
    // ignore_incompatible:true 后可达（completed + result.success:true）。──
    // 两任务都须跑到终态（快），单活动槽此时空闲（heavy_cancel 已取消）。
    const std::string conflict_min = R"({"target":{"item":"diamond_sword","enchants":[)"
                                     R"({"id":"sharpness","level":5},{"id":"smite","level":5}]},)"
                                     R"("algorithm":"dp_merge","max_solutions":1)";
    auto submit_conflict = [&](const std::string& body, const char* label) {
        auto r = app.call(Method::Post, "/api/tasks", body);
        expect(r.status == 202, std::string("conflict task ") + label + " submit 202");
        std::string tid;
        if (r.status == 202) {
            auto b = Json::parse(r.body);
            tid = b["task_id"].as<std::string>();
        }
        bool done = false;
        for (int i = 0; i < 50 && !done; ++i) {
            auto st = app.call(Method::Get, "/api/tasks/" + tid);
            if (st.status == 200) {
                auto sj = Json::parse(st.body);
                done = sj["state"].as<std::string>() != "running";
            }
            if (!done)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(done, std::string("conflict task ") + label + " reached terminal state");
        auto st = app.call(Method::Get, "/api/tasks/" + tid);
        return Json::parse(st.body);
    };
    auto strict_j = submit_conflict(conflict_min + "}", "strict");
    expect(strict_j["state"].as<std::string>() == "completed", "strict conflict task completed (infeasible, not crashed)");
    expect(strict_j["result"].has("success") && strict_j["result"]["success"].as<bool>() == false,
           "strict conflict result success:false");
    auto lax_j = submit_conflict(conflict_min + R"(,"ignore_incompatible":true})", "lax");
    expect(lax_j["state"].as<std::string>() == "completed", "ignore_incompatible task completed");
    expect(lax_j["result"].has("success") && lax_j["result"]["success"].as<bool>() == true,
           "ignore_incompatible result success:true");

    // 错误类型 → 400 INVALID_TASK（与 max_threads 同一校验路径）。
    auto badii = app.call(Method::Post, "/api/tasks",
                          R"({"target":{"item":"diamond_sword","enchants":)"
                          R"([{"id":"sharpness","level":5}]},"ignore_incompatible":"x"})");
    expect(badii.status == 400 && badii.body.find("INVALID_TASK") != std::string::npos,
           "task ignore_incompatible wrong type 400 INVALID_TASK");

    {
        // ── Batch C: 暂停/继续 —— 用 executor 状态做确定性观测 ──
        // 18-ench dp_merge 的完整求解很长（>40s，原测试从不等待其自然完成），故
        // 本测试不等待 completed：pause → 算法真正冻结（solve_progress().state ==
        // Paused，executor 冻结在暂停点）→ has_active 仍 true + 新提交 409 + status
        // state:"paused" → resume → 算法恢复（state == Running，web 回 running）→
        // DELETE 取消（继续后的慢求解仍可取消）。重试循环吸收 pause 落在 executor
        // 发布窗口的竞态（pause_solve 空操作 → 求解跑完）：该情形下取消当前任务
        // 释放单槽后换新任务。
        std::string paused_id;
        bool paused_observed = false;
        for (int attempt = 0; attempt < 3 && !paused_observed; ++attempt) {
            auto r = app.call(Method::Post, "/api/tasks", heavy);
            expect(r.status == 202, "pause-resume task submit 202");
            paused_id = Json::parse(r.body)["task_id"].as<std::string>();
            auto pa = app.call(Method::Post, "/api/tasks/" + paused_id + "/pause");
            expect(pa.status == 200 && pa.body.find("ok") != std::string::npos, "pause 200 ok");
            for (int i = 0; i < 30 && !paused_observed; ++i) {
                if (app.ctx.solve_progress().state == algorithm::AlgorithmState::Paused) {
                    paused_observed = true;
                    break;
                }
                auto st = app.call(Method::Get, "/api/tasks/" + paused_id);
                if (st.status != 200)
                    break;
                auto sj = Json::parse(st.body);
                // pause 未命中（发布窗口竞态）→ 求解直接跑完 → 取消并重试。
                if (sj["state"].as<std::string>() == "completed" || sj["state"].as<std::string>() == "failed")
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!paused_observed) {
                // 释放单槽：取消竞态任务的慢求解。
                (void)app.call(Method::Delete, "/api/tasks/" + paused_id);
                bool gone = false;
                for (int i = 0; i < 50 && !gone; ++i) {
                    auto st = app.call(Method::Get, "/api/tasks/" + paused_id);
                    if (st.status == 200) {
                        auto sj = Json::parse(st.body);
                        gone = sj["state"].as<std::string>() == "cancelled";
                    }
                    if (!gone)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
        expect(paused_observed, "executor reached Paused state");

        // 暂停中：web 状态 paused；has_active 仍 true（executor 占着单活动槽）。
        auto st_p = app.call(Method::Get, "/api/tasks/" + paused_id);
        expect(st_p.status == 200 && st_p.body.find("\"state\":\"paused\"") != std::string::npos,
               "status while paused state=paused");
        expect(app.solve->has_active(), "has_active true while paused");
        auto dup_paused = app.call(Method::Post, "/api/tasks", R"({
            "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
            "algorithm":"dp_merge",
            "max_solutions":1
        })");
        expect(dup_paused.status == 409, "second POST while paused 409");

        // 主流程：resume → executor 回到 Running → web 回 running。
        auto rs = app.call(Method::Post, "/api/tasks/" + paused_id + "/resume");
        expect(rs.status == 200 && rs.body.find("ok") != std::string::npos, "resume 200 ok");
        bool resumed = false;
        for (int i = 0; i < 30 && !resumed; ++i) {
            if (app.ctx.solve_progress().state == algorithm::AlgorithmState::Running) {
                resumed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(resumed, "executor resumed to Running");
        auto st_r = app.call(Method::Get, "/api/tasks/" + paused_id);
        expect(st_r.status == 200 && st_r.body.find("\"state\":\"running\"") != std::string::npos,
               "status after resume state=running");

        // 继续后的慢求解可正常取消（不等待自然完成）。
        auto canc_paused = app.call(Method::Delete, "/api/tasks/" + paused_id);
        expect(canc_paused.status == 200, "cancel resumed task 200");
        bool canc_done = false;
        for (int i = 0; i < 50 && !canc_done; ++i) {
            auto st = app.call(Method::Get, "/api/tasks/" + paused_id);
            if (st.status == 200) {
                auto sj = Json::parse(st.body);
                canc_done = sj["state"].as<std::string>() == "cancelled";
            }
            if (!canc_done)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(canc_done, "resumed task cancelled");

        // 状态校验：resume 非 paused → 409；pause 非 running → 409；未知 → 404。
        auto bad_rs = app.call(Method::Post, "/api/tasks/" + paused_id + "/resume");
        expect(bad_rs.status == 409 && bad_rs.body.find("TASK_NOT_RESUMABLE") != std::string::npos,
               "resume cancelled task 409 TASK_NOT_RESUMABLE");
        auto bad_pa = app.call(Method::Post, "/api/tasks/" + paused_id + "/pause");
        expect(bad_pa.status == 409 && bad_pa.body.find("TASK_NOT_PAUSABLE") != std::string::npos,
               "pause cancelled task 409 TASK_NOT_PAUSABLE");
        auto no_pa = app.call(Method::Post, "/api/tasks/nope/pause");
        expect(no_pa.status == 404 && no_pa.body.find("TASK_NOT_FOUND") != std::string::npos, "pause unknown task 404");
        auto no_rs = app.call(Method::Post, "/api/tasks/nope/resume");
        expect(no_rs.status == 404, "resume unknown task 404");
    }

    // ── §12.1: unload while a solve is active → 409 TASK_ACTIVE ──
    // Deterministic by construction: the unload handler checks has_active()
    // BEFORE taking the gate (a gate-first check would only ever see a
    // completed task, since the solve worker holds the gate for its whole
    // _ctx window — the 409 would depend on racing the worker's first gate
    // acquisition).  So a plain submit-then-unload always lands while the
    // task is Running → 409, with no thread parking and no wake-order
    // assumptions.  The worker proceeds (the gate stays held across the
    // heavy solve), so the rest of the heavy-task flow below is unchanged.
    auto heavy_resp = app.call(Method::Post, "/api/tasks", heavy);
    expect(heavy_resp.status == 202, "heavy task submit 202");
    auto hb = Json::parse(heavy_resp.body);
    std::string heavy_id = hb["task_id"].as<std::string>();
    auto un = app.call(Method::Post, "/api/algorithms/unload", R"({"name":"dp_merge"})");
    expect(un.status == 409 && un.body.find("TASK_ACTIVE") != std::string::npos, "unload while solve active 409 TASK_ACTIVE");

    // Immediate second POST while the heavy task is Running → 409 (single slot).
    auto dup = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(dup.status == 409, "second POST while active 409");

    // SSE subscribe while the heavy task is Running → 200 and exactly one hub
    // subscription registered for it (Fix 3 coverage for the events path).
    auto heav_ev = app.call(Method::Get, "/api/tasks/" + heavy_id + "/events");
    expect(heav_ev.status == 200 && heav_ev.is_stream && heav_ev.content_type == "text/event-stream",
           "heavy events stream response");
    expect(app.hub.subscriber_count(heavy_id) == 1, "heavy task SSE subscription registered");

    // Unknown task → 404 for both status and events.
    auto no = app.call(Method::Get, "/api/tasks/nope");
    expect(no.status == 404, "unknown task status 404");
    auto noev = app.call(Method::Get, "/api/tasks/nope/events");
    expect(noev.status == 404, "unknown task events 404");

    // The heavy task may still be running; it is cancelled + joined when the
    // TestApp (owning WebSolveService) is destroyed at the end of main().
}

void test_history(TestApp& app) {
    // /api/history 替代已删除的 /api/logs*（计划 B Task B3）。
    // 契约（设计文档 §2.4）：{"events":[...],"total":N,"next_offset":M}，最新在前；
    // ?offset=N&limit=M 分页（offset 从 0）；?after_seq=N 游标（只返回 seq > N，
    // 增量拉取，作为过滤先于 offset/limit 切片）。
    // 本用例须在首个任务提交前运行（空历史断言确定性成立）。

    // ── 1. 空历史：events 空数组 + total=0 + next_offset=0 ──
    auto empty = app.call(Method::Get, "/api/history");
    expect(empty.status == 200, "history 200");
    auto ej = Json::parse(empty.body);
    expect(ej["events"].type() == JsonType::Array && ej["events"].as_array().empty(), "history empty events array");
    expect(ej["total"].as<int64_t>() == 0, "history total 0 when empty");
    expect(ej["next_offset"].as<int64_t>() == 0, "history next_offset 0 when empty");

    // ── 2. 提交一个已完成任务（Submitted + Completed 两条事件）──
    auto t = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(t.status == 202, "history task submit 202");
    std::string id = Json::parse(t.body)["task_id"].as<std::string>();
    bool done = false;
    for (int i = 0; i < 50 && !done; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + id);
        if (st.status == 200)
            done = Json::parse(st.body)["state"].as<std::string>() != "running";
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(done, "history task reached terminal state");

    // ── 3. 普通查询：事件字段全量序列化 + total=2 + 最新在前（seq 严格递减）──
    // 状态字可观察与事件记录间有微小窗口（终态提交 → record_solve_event），
    // 有界重试吸收（与 test_calculator 的 B2 断言同款）；断言全在循环外。
    bool saw_completed = false, completed_fields = false;
    bool seq_desc = true, all_fields = true, checked_any = false;
    bool result_null_others = true; // 非 Completed 事件 result 为 null
    int64_t total_n = -1;
    size_t arr_n = 0;
    for (int i = 0; i < 50 && !saw_completed; ++i) {
        auto h = app.call(Method::Get, "/api/history");
        if (h.status != 200)
            continue;
        auto hj = Json::parse(h.body);
        total_n = hj["total"].as<int64_t>();
        auto arr = hj["events"].as_array();
        arr_n = arr.size();
        int64_t prev = INT64_MAX;
        for (const auto& ev : arr) {
            checked_any = true;
            for (const char* f : {"seq", "type", "task_id", "target", "algorithm", "mode", "timestamp_ms", "total_level_cost",
                                  "total_exp_cost", "solution_count", "computation_ms", "error_message", "result"})
                all_fields = all_fields && ev.has(f);
            auto s = ev["seq"].as<int64_t>();
            seq_desc = seq_desc && s < prev;
            prev = s;
            if (ev["type"].as<std::string>() == "completed") {
                saw_completed = true;
                // C1：Completed 事件 result 为完整结果 JSON 对象（含 solutions 数组）。
                const auto r = ev["result"];
                completed_fields = ev["task_id"].as<std::string>() == id && ev["total_level_cost"].as<int64_t>() > 0 &&
                                   ev["computation_ms"].as<int64_t>() >= 0 && r.type() == JsonType::Object &&
                                   r.has("solutions") && r["solutions"].type() == JsonType::Array &&
                                   !r["solutions"].as_array().empty();
            } else {
                result_null_others = result_null_others && ev["result"].is_null();
            }
        }
        if (!saw_completed)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(saw_completed, "history contains a completed event");
    expect(completed_fields, "completed event carries task_id/costs/ms + result solutions");
    expect(result_null_others, "non-completed events carry null result");
    expect(seq_desc, "events newest-first (seq strictly descending)");
    expect(checked_any && all_fields, "every event serializes all SolveHistoryEvent fields");
    expect(total_n == 2, "history total == 2 (submitted+completed)");
    expect(arr_n == 2, "history events size 2");

    // ── 4. 分页：offset=0&limit=1 → 恰 1 条 + next_offset=1；offset 越界/limit=0 ──
    auto pg = app.call(Method::Get, "/api/history?offset=0&limit=1");
    expect(pg.status == 200, "history page 200");
    auto pj = Json::parse(pg.body);
    auto pev = pj["events"].as_array();
    expect(pev.size() == 1, "limit=1 returns exactly one event");
    expect(pj["next_offset"].as<int64_t>() == 1, "next_offset = offset + page size");
    auto seq0 = pev[0]["seq"].as<int64_t>();
    expect(pev[0]["type"].as<std::string>() == "completed", "first page event is the completed event");

    auto off1 = app.call(Method::Get, "/api/history?offset=1&limit=1");
    expect(off1.status == 200, "history offset=1 200");
    auto o1 = Json::parse(off1.body);
    auto o1v = o1["events"].as_array();
    expect(o1v.size() == 1, "offset=1 returns the second event");
    expect(o1v[0]["seq"].as<int64_t>() < seq0, "offset=1 event is older than the first");
    expect(o1["next_offset"].as<int64_t>() == 2, "next_offset = offset + page size (page 2)");

    // offset 越界（≥ 条数）→ 空数组，next_offset 停在 offset。
    auto past = app.call(Method::Get, "/api/history?offset=99&limit=5");
    expect(past.status == 200, "history offset past end 200");
    auto pj2 = Json::parse(past.body);
    expect(pj2["events"].as_array().empty(), "offset past end returns empty events");
    expect(pj2["next_offset"].as<int64_t>() == 99, "offset past end next_offset stays at offset");

    // limit=0 → 空数组（显式零条），非全量转储。
    auto zero = app.call(Method::Get, "/api/history?limit=0");
    expect(zero.status == 200, "limit=0 200");
    auto zj = Json::parse(zero.body);
    expect(zj["events"].as_array().empty(), "limit=0 empty events");
    expect(zj["next_offset"].as<int64_t>() == 0, "limit=0 next_offset 0");

    // ── 5. 游标：after_seq=<最新 seq-1> → 只返回 seq > N 的事件（恰该条）──
    auto cur = app.call(Method::Get, "/api/history?after_seq=" + std::to_string(seq0 - 1));
    expect(cur.status == 200, "history cursor 200");
    auto cj = Json::parse(cur.body);
    auto carr = cj["events"].as_array();
    expect(carr.size() == 1, "after_seq keeps exactly the events with seq > N");
    expect(carr[0]["seq"].as<int64_t>() == seq0, "after_seq page contains the expected event");
    expect(cj["next_offset"].as<int64_t>() == 1, "cursor page next_offset = page size");

    // 游标 + 分页叠加：after_seq 先过滤，offset/limit 在过滤结果上切片。
    auto cur_pg = app.call(Method::Get, "/api/history?after_seq=0&limit=1");
    expect(cur_pg.status == 200, "cursor+limit 200");
    auto cp = Json::parse(cur_pg.body);
    auto cpv = cp["events"].as_array();
    expect(cpv.size() == 1 && cpv[0]["seq"].as<int64_t>() == seq0, "cursor+limit combines as filter-then-slice");

    // ── 6. 参数校验：非数字/负数/溢出 → 400 INVALID_FIELD ──
    auto bad_limit = app.call(Method::Get, "/api/history?limit=x");
    expect(bad_limit.status == 400 && bad_limit.body.find("INVALID_FIELD") != std::string::npos,
           "invalid limit 400 INVALID_FIELD");
    auto neg_limit = app.call(Method::Get, "/api/history?limit=-1");
    expect(neg_limit.status == 400 && neg_limit.body.find("code") != std::string::npos, "negative limit 400");
    auto ovf = app.call(Method::Get, "/api/history?limit=99999999999999999999");
    expect(ovf.status == 400 && ovf.body.find("code") != std::string::npos, "limit overflow 400");
    auto bad_offset = app.call(Method::Get, "/api/history?offset=abc");
    expect(bad_offset.status == 400 && bad_offset.body.find("INVALID_FIELD") != std::string::npos,
           "invalid offset 400 INVALID_FIELD");
    auto neg_offset = app.call(Method::Get, "/api/history?offset=-3");
    expect(neg_offset.status == 400 && neg_offset.body.find("code") != std::string::npos, "negative offset 400");
    auto bad_seq = app.call(Method::Get, "/api/history?after_seq=x");
    expect(bad_seq.status == 400 && bad_seq.body.find("INVALID_FIELD") != std::string::npos,
           "invalid after_seq 400 INVALID_FIELD");
    auto neg_seq = app.call(Method::Get, "/api/history?after_seq=-1");
    expect(neg_seq.status == 400 && neg_seq.body.find("code") != std::string::npos, "negative after_seq 400");

    // ── 7. /api/logs* 已删除 → 404 ──
    auto gone = app.call(Method::Get, "/api/logs");
    expect(gone.status == 404 && gone.body.find("code") != std::string::npos, "/api/logs deleted 404");
    auto gone_ev = app.call(Method::Get, "/api/logs/events");
    expect(gone_ev.status == 404 && gone_ev.body.find("code") != std::string::npos, "/api/logs/events deleted 404");
}

// ── Fake StreamChannel: captures every frame delivered to the "connection" ──
struct FakeChannel : web::StreamChannel {
    std::mutex mtx;
    std::vector<std::string> frames;
    std::function<void()> on_close_cb;
    bool close_fired = false;
    // post_frame 可被 worker 线程经 hub publish 并发调用（初始 progress 帧/终态帧），
    // 而测试线程会同时迭代 frames —— 必须互斥，否则 vector 迭代器并发失效
    // （MSVC Debug 断言 "can't increment invalidated vector iterator"）。
    void post_frame(std::string f) override {
        std::lock_guard<std::mutex> lk(mtx);
        frames.push_back(std::move(f));
    }
    void on_close(std::function<void()> cb) override { on_close_cb = std::move(cb); }
    std::vector<std::string> snapshot() {
        std::lock_guard<std::mutex> lk(mtx);
        return frames;
    }
    /// 模拟连接关闭：触发控制器注册的 on_close 回调（触发一次后清空）。
    void fire_close() {
        close_fired = true;
        if (on_close_cb) {
            auto cb = std::move(on_close_cb);
            on_close_cb = nullptr;
            cb();
        }
    }
};

/// WebModule 组装测试：/ → 307 + Location、/public/* → 静态、其余 → Router。
void test_web_module(BesqContext& ctx) {
    web::WebModule module(ctx);
    module.set_static_resources({{"/index.html", {"text/html", "<h1>hi</h1>"}}});

    HttpRequest root;
    root.method = Method::Get;
    root.path = "/";
    auto r0 = module.dispatch(root);
    expect(r0.status == 307, "root 307 redirect");
    expect(r0.header_value("Location") == "/public/index.html", "root Location header");

    HttpRequest idx;
    idx.method = Method::Get;
    idx.path = "/public/index.html";
    auto r1 = module.dispatch(idx);
    expect(r1.status == 200, "public index 200");
    expect(r1.content_type == "text/html", "public index content type");
    expect(r1.body.find("<h1>hi</h1>") != std::string::npos, "public index body served");

    HttpRequest st;
    st.method = Method::Get;
    st.path = "/api/status";
    auto r2 = module.dispatch(st);
    expect(r2.status == 200, "api/status routed to controller 200");

    HttpRequest no;
    no.method = Method::Get;
    no.path = "/nope";
    auto r3 = module.dispatch(no);
    expect(r3.status == 404, "unknown api route 404");

    HttpRequest pn;
    pn.method = Method::Get;
    pn.path = "/public/nope";
    auto r4 = module.dispatch(pn);
    expect(r4.status == 404, "unknown static asset 404");

    // ── C2: effective-port injection (real WebModule → SettingsController
    //    wiring).  Without injection the settings gui_port is the configured
    //    value; after set_effective_port() it reports the injected port —
    //    exactly what main.cpp does with HttpServer::port() post-bind. ──
    HttpRequest st0;
    st0.method = Method::Get;
    st0.path = "/api/settings";
    auto s0 = module.dispatch(st0);
    auto sj0 = Json::parse(s0.body);
    expect(s0.status == 200 && sj0.has("gui_port") &&
               sj0["gui_port"].as<int64_t>() == static_cast<int64_t>(AppConfig::get().gui_port),
           "no injection → gui_port stays the configured value");

    module.set_effective_port(4321);
    HttpRequest st1;
    st1.method = Method::Get;
    st1.path = "/api/settings";
    auto s1 = module.dispatch(st1);
    auto sj1 = Json::parse(s1.body);
    expect(s1.status == 200 && sj1["gui_port"].as<int64_t>() == 4321, "injected effective port overrides configured gui_port");
}

/// StreamChannel 桥接测试：CalculatorController::events 把 req.stream 上的帧投递通道
/// 接进 SseHub 订阅 → hub.publish 把帧送到 FakeChannel。（证明 events→hub→channel 链路。）
void test_stream_channel(TestApp& app) {
    // 提交一个任务：它在测试的微秒级窗口内保持 Running（dp_merge 至少耗时毫秒级），
    // 订阅 + 手动 publish 期间不会被 worker 完成/取消订阅。
    auto light = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(light.status == 202, "channel task submit 202");
    auto lb = Json::parse(light.body);
    std::string id = lb["task_id"].as<std::string>();

    auto fake = std::make_shared<FakeChannel>();
    CalculatorController ctrl(*app.solve, app.hub);
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/api/tasks/" + id + "/events";
    req.stream = fake;
    PathParams pp;
    pp.kv.emplace_back("id", id);
    auto r = ctrl.events(req, pp);
    expect(r.status == 200 && r.is_stream, "events stream response via channel");
    expect(app.hub.subscriber_count(id) >= 1, "hub subscription registered for channel");

    // I-2b: subscribing immediately delivers one initial progress frame from
    // svc.status(id) — a late subscriber sees the task's last-known progress
    // instead of a silent window until the next hub publish. Frame shape
    // matches the worker's (spec §7): event: progress + {"type":"progress",...}.
    bool initial = false;
    double initial_progress = -1.0;
    for (const auto& f : fake->snapshot()) {
        if (f.rfind("event: progress", 0) == 0) {
            auto data = f.find("data: ");
            if (data == std::string::npos)
                continue;
            auto payload = Json::parse(f.substr(data + 6));
            if (payload.has("type") && payload["type"].as<std::string>() == "progress" && payload.has("progress")) {
                initial = true;
                initial_progress = payload["progress"].as<double>();
            }
        }
    }
    expect(initial, "initial progress frame delivered on subscribe");
    expect(initial_progress >= 0.0 && initial_progress <= 1.0, "initial progress in 0..1 range");

    app.hub.publish(id, "data: x\n\n");
    bool delivered = false;
    for (const auto& f : fake->snapshot())
        if (f == "data: x\n\n")
            delivered = true;
    expect(delivered, "published frame delivered to StreamChannel");

    // 连接关闭 → on_close 回调 → 从 hub 退订该任务的订阅（SubId 幂等）。
    // 若任务恰在此时完成，WebSolveService 已 unsubscribe_all，计数同样为 0 ——
    // 两种路径都让订阅数归零，断言不抖动。
    expect(fake->on_close_cb != nullptr, "on_close callback registered on task channel");
    fake->fire_close();
    expect(app.hub.subscriber_count(id) == 0, "task subscription dropped after close");

    // 取消任务，避免占用单活动槽影响后续测试。
    auto c = app.call(Method::Delete, "/api/tasks/" + id);
    expect(c.status == 200, "cancel channel task");
}

/// SseHub::clear()（Fix 1 的公开 API）：清空全部订阅。clear 后订阅计数归零，
/// publish 不再送达任何通道。WebModule 析构体在 Impl 成员析构前调用它来排空 hub。
/// SseHub 最后一帧重放（根治"订阅晚于发布 → 帧丢失"竞态，见
/// test_web_integration P2 记录）：迟到订阅者立即收到该任务最近一帧。
void test_hub_replay_late_subscriber(TestApp& app) {
    app.hub.publish("replay_t", "event: completed\ndata: {}\n\n");
    app.hub.publish("replay_t", "data: last\n\n");
    std::string got;
    auto sub = app.hub.subscribe(
        "replay_t", [&](const std::string&, std::string f) { got = std::move(f); },
        true); // 任务键语义：开启重放
    expect(got == "data: last\n\n", "late subscriber receives the last published frame");
    // 新发布仍正常广播到既有订阅者。
    got.clear();
    app.hub.publish("replay_t", "data: next\n\n");
    expect(got == "data: next\n\n", "subsequent publish reaches subscriber");
    app.hub.unsubscribe("replay_t", sub);
}

void test_hub_clear(TestApp& app) {
    // LogsController 已删除（B3），hub 只剩任务键：直接订阅一个合成键验证
    // clear() 语义（与键无关）：清空全部订阅、此后 publish 不再送达。
    std::string got;
    auto sub = app.hub.subscribe("clear_t", [&](const std::string&, std::string f) { got = std::move(f); });
    expect(app.hub.subscriber_count("clear_t") == 1, "subscription registered before clear");

    app.hub.publish("clear_t", "data: hi\n\n");
    expect(got == "data: hi\n\n", "frame delivered before clear");

    app.hub.clear();
    expect(app.hub.subscriber_count("clear_t") == 0, "hub cleared all subscriptions");

    got.clear();
    app.hub.publish("clear_t", "data: nope\n\n");
    expect(got.empty(), "no frame delivered after clear");
    (void)sub;
}

/// 连接关闭钩子测试（确定性证明 on_close → 退订 的接线）：
/// LogsController 已删除（B3）后 hub 只剩任务键——用重任务（18 自定义魔咒的
/// dp_merge，秒级求解）占住单槽：订阅建立后任务必仍在 Running，此时 fire_close
/// 后订阅数归零的唯一路径就是 CalculatorController::events 注册的 on_close 回调
/// （任务未完成，WebSolveService 不会 unsubscribe_all）。
void test_stream_close_hook(TestApp& app) {
    // 前置：种子 18 个剑适用自定义魔咒（同 test_calculator 的载荷），使重任务
    // 在订阅窗口内确定性保持 Running。diamond_sword 未被 test_profiles 删除。
    for (int i = 0; i < 18; ++i) {
        EnchInfo info;
        info.id = NSID("test:hook_" + std::to_string(i));
        info.name = "Hook " + std::to_string(i);
        info.max_level = 5;
        info.multiplier = 1;
        info.supported_items.insert(NSID("#minecraft:swords"));
        expect(app.ctx.add_enchantment(info), "close-hook seed ench " + std::to_string(i));
    }
    std::string heavy = R"({"target":{"item":"diamond_sword","enchants":[)";
    for (int i = 0; i < 18; ++i) {
        if (i)
            heavy += ",";
        heavy += R"({"id":"test:hook_)" + std::to_string(i) + R"(","level":5})";
    }
    heavy += R"(]},"algorithm":"dp_merge"})";
    auto r = app.call(Method::Post, "/api/tasks", heavy);
    expect(r.status == 202, "close-hook task submit 202");
    std::string id = Json::parse(r.body)["task_id"].as<std::string>();

    auto fake = std::make_shared<FakeChannel>();
    CalculatorController ctrl(*app.solve, app.hub);
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/api/tasks/" + id + "/events";
    req.stream = fake;
    PathParams pp;
    pp.kv.emplace_back("id", id);
    auto st = ctrl.events(req, pp);
    expect(st.status == 200 && st.is_stream, "close-hook events stream response via channel");
    expect(app.hub.subscriber_count(id) == 1, "close-hook subscription registered");

    // 模拟客户端断开 → on_close 回调 → 退订（任务仍在 Running，唯一清零路径）。
    expect(fake->on_close_cb != nullptr, "on_close callback registered on task channel");
    fake->fire_close();
    expect(app.hub.subscriber_count(id) == 0, "task subscription dropped after close");

    // 退订后 publish 不再送达该通道（死连接回调已从 hub 移除）。
    app.hub.publish(id, "data: nope\n\n");
    bool leaked = false;
    for (const auto& f : fake->snapshot())
        if (f == "data: nope\n\n")
            leaked = true;
    expect(!leaked, "no frame delivered after close");

    // 清理单活动槽：取消并等待 cancelled（慢求解不等待自然完成，同 test_calculator）。
    auto c = app.call(Method::Delete, "/api/tasks/" + id);
    expect(c.status == 200, "cancel close-hook task");
    bool cancelled = false;
    for (int i = 0; i < 50 && !cancelled; ++i) {
        auto ts = app.call(Method::Get, "/api/tasks/" + id);
        if (ts.status == 200) {
            auto tj = Json::parse(ts.body);
            cancelled = tj["state"].as<std::string>() == "cancelled";
        }
        if (!cancelled)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(cancelled, "close-hook task cancelled");
}

/// §12.1 complement: the `failed` SSE frame's byte format.
///
/// The worker's real failure path (state=failed + error) is deterministic and
/// covered by the snapshot test in test_calculator, but the failed FRAME can
/// only be observed by a subscriber that is already registered when the worker
/// throws — and every deterministic failure (unknown ench/equip/algorithm,
/// mode mismatch) happens inside build_request/solve within ~100µs of submit,
/// far faster than any second HTTP request can reach the hub over the real
/// transport. This test pins the exact frame the worker publishes
/// (`event: failed` + `data: {"type":"failed","error":...}` — spec §7) by
/// pushing the worker's own frame bytes through the SseHub to a subscribed
/// StreamChannel and asserting delivery + field shape.
void test_failed_frame_shape(TestApp& app) {
    auto light = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(light.status == 202, "failed-shape task submit 202");
    auto lb = Json::parse(light.body);
    std::string id = lb["task_id"].as<std::string>();

    auto fake = std::make_shared<FakeChannel>();
    CalculatorController ctrl(*app.solve, app.hub);
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/api/tasks/" + id + "/events";
    req.stream = fake;
    PathParams pp;
    pp.kv.emplace_back("id", id);
    auto r = ctrl.events(req, pp);
    expect(r.status == 200 && r.is_stream, "failed-shape events stream response");

    // The worker's exact failed frame format (WebSolveService::sse_frame):
    //   event: failed\ndata: {"type":"failed","error":"..."}\n\n
    Json payload = Json::object();
    payload["type"] = Json("failed");
    payload["error"] = Json("forced failure");
    const std::string frame = "event: failed\ndata: " + payload.to_string() + "\n\n";
    app.hub.publish(id, frame);

    bool got = false, has_type = false, has_err = false;
    for (const auto& f : fake->snapshot()) {
        if (f != frame)
            continue;
        got = true;
        auto d = f.find("data: ");
        if (d != std::string::npos) {
            auto fj = Json::parse(f.substr(d + 6));
            if (fj["type"].as<std::string>() == "failed")
                has_type = true;
            if (fj["error"].as<std::string>() == "forced failure")
                has_err = true;
        }
    }
    expect(got, "failed frame bytes delivered through hub");
    expect(has_type && has_err, "failed frame carries type/error fields");

    auto c = app.call(Method::Delete, "/api/tasks/" + id);
    expect(c.status == 200, "cancel failed-shape task");
}

/// T2: 任务算法诊断事件流——completed 任务的状态响应携带 diagnostics 数组
/// （含 progress/exit 事件）与 diag_exit（exit 结构化 KV）；SSE 订阅收到
/// "diag" 帧。单活动槽保证观察者绑定的任务 = 当前唯一运行任务（409 单槽
/// 行为由 test_calculator 覆盖）。
void test_task_diagnostics(TestApp& app) {
    // ── 1. completed 任务的状态快照含诊断字段 ──
    auto light = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(light.status == 202, "diag task submit 202");
    auto lb = Json::parse(light.body);
    std::string id = lb["task_id"].as<std::string>();
    bool done = false;
    for (int i = 0; i < 50 && !done; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + id);
        if (st.status == 200) {
            auto sj = Json::parse(st.body);
            done = sj["state"].as<std::string>() != "running";
        }
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(done, "diag task reached terminal state");

    auto st = app.call(Method::Get, "/api/tasks/" + id);
    expect(st.status == 200, "diag task status 200");
    auto body = Json::parse(st.body);
    expect(body["diagnostics"].type() == JsonType::Array && !body["diagnostics"].as_array().empty(),
           "status carries non-empty diagnostics array");
    bool has_progress = false, has_exit = false;
    for (const auto& d : body["diagnostics"].as_array()) {
        std::string kind = d.has("kind") ? d["kind"].as<std::string>() : "";
        if (kind == "progress")
            has_progress = true;
        if (kind == "exit")
            has_exit = true;
    }
    expect(has_progress, "diagnostics contains a progress event");
    expect(has_exit, "diagnostics contains an exit event");

    expect(body.has("diag_exit"), "status carries diag_exit");
    auto ex = body["diag_exit"];
    for (const char* f : {"algorithm", "status", "wall_ms", "counters", "diag"})
        expect(ex.has(f), std::string("diag_exit field ") + f);
    expect(ex["algorithm"].as<std::string>() == "dp_merge", "diag_exit algorithm name");
    expect(ex["counters"].has("nodes_visited") && ex["counters"].has("nodes_pruned") && ex["counters"].has("steps_forged"),
           "diag_exit counters keys");
    expect(ex["diag"].type() == JsonType::Object && !ex["diag"].as_object().empty(), "diag_exit diag KV non-empty");
    expect(ex["diag"].has("solution_cost") && ex["diag"].has("diag_schema_version"),
           "diag_exit diag KV carries solution_cost + schema version");

    // ── 2. SSE 订阅收到 "diag" 帧（含 exit 帧）──
    auto light2 = app.call(Method::Post, "/api/tasks", R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })");
    expect(light2.status == 202, "diag-sse task submit 202");
    auto l2 = Json::parse(light2.body);
    std::string id2 = l2["task_id"].as<std::string>();

    auto fake = std::make_shared<FakeChannel>();
    CalculatorController ctrl(*app.solve, app.hub);
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/api/tasks/" + id2 + "/events";
    req.stream = fake;
    PathParams pp;
    pp.kv.emplace_back("id", id2);
    auto r = ctrl.events(req, pp);
    expect(r.status == 200 && r.is_stream, "diag-sse events stream response");

    // 轮询 FakeChannel 直到出现 event: diag 帧。worker 在 solve 尾部（completed
    // 帧之前）发布 exit 帧；轻量 dp_merge 毫秒级完成，订阅远早于首个 diag
    // 发布落地（与 test_stream_channel 同一时序范式）。
    bool got_diag = false, got_exit_frame = false;
    for (int i = 0; i < 50 && !got_exit_frame; ++i) {
        for (const auto& f : fake->snapshot()) {
            if (f.rfind("event: diag", 0) != 0)
                continue;
            got_diag = true;
            auto d = f.find("data: ");
            if (d == std::string::npos)
                continue;
            auto dj = Json::parse(f.substr(d + 6));
            if (dj.has("kind") && dj["kind"].as<std::string>() == "exit")
                got_exit_frame = true;
        }
        if (!got_exit_frame)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(got_diag, "SSE delivered a diag frame");
    expect(got_exit_frame, "SSE diag stream contains the exit frame");

    // 释放单活动槽（任务届时多半已完成；cancel 对已完成任务是 200 no-op）。
    auto c = app.call(Method::Delete, "/api/tasks/" + id2);
    expect(c.status == 200, "cancel diag-sse task");
}

/// §12.1: 500 with the envelope — Router maps any non-WebHttpError
/// std::exception to 500 INTERNAL_ERROR. No production endpoint can be forced
/// to throw on demand (all handler throws are pre-mapped WebHttpErrors or
/// JsonExceptions), so the catch path is asserted directly with a throwing
/// test controller.
class BoomController : public HttpController<BoomController> {
public:
    using Self = BoomController;
    static constexpr auto route_defs() { return std::array{BESQ_ROUTE(Get, "/boom", boom)}; }
    Response boom(const HttpRequest&) { throw std::runtime_error("boom:forced"); }
};

void test_router_500() {
    Router router;
    router.register_controller<BoomController>();
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/boom";
    auto r = router.dispatch(req);
    expect(r.status == 500, "uncaught handler exception → 500");
    expect(r.body.find("\"ok\":false") != std::string::npos, "500 envelope ok:false");
    expect(r.body.find("\"code\":\"INTERNAL_ERROR\"") != std::string::npos, "500 envelope code INTERNAL_ERROR");
    expect(r.body.find("\"message\":\"internal server error\"") != std::string::npos, "500 envelope generic message");
    expect(r.body.find("boom:forced") == std::string::npos, "500 envelope does not leak internal exception text");
}
} // namespace

TEST_CASE("test_web_api") {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();
    TestApp app(ctx);
    test_history(app); // /api/history：空历史 → 提交任务 → 普通/分页/游标/400 + /api/logs 404
                       // （须在首个任务提交前运行：空历史断言确定性成立）
    test_health(app);
    test_status(app);
    test_settings(app);
    test_profiles(app);
    test_profile_actions(app); // §12.1: actions + tags/ench round-trips
    test_enchantables(app);    // §12.1: /enchantables/{item} 适用附魔查询
    test_fs(app);              // 目录选择器：/api/fs/list（根锁 + 非法路径 400）
    test_algorithms(app);
    test_stream_channel(app);             // 须在 test_calculator 之前（单活动槽）
    test_failed_frame_shape(app);         // failed SSE 帧字节格式（hub 级，确定性）
    test_stream_close_hook(app);          // on_close → 退订 接线测试（重任务确定性）
    test_hub_clear(app);                  // SseHub::clear() 清空全部订阅
    test_hub_replay_late_subscriber(app); // 迟到订阅者重放（SSE 终态帧竞态根治）
    test_task_diagnostics(app);           // T2: 任务诊断事件流（diagnostics/diag_exit + SSE diag 帧）
    test_calculator(app);                 // §12.1: failed/cancelled 快照、任务字段、unload-409
    test_web_module(app.ctx);
    test_router_500(); // §12.1: 未捕获异常 → 500 INTERNAL_ERROR envelope
    TEST_PASS("test_web_api");
}

/// A7b 回归：WebSolveService 关机竞态（WSL 下 ~WebSolveService 内
/// std::terminate → exit 134）。
///
/// test_calculator 的重任务（18-ench dp_merge，自然求解 >40s）在 case 结束
/// 时故意保持 Running，由 TestApp 析构（→ ~WebSolveService）取消并 join。
/// 发布窗口竞态：worker 在 executor 句柄发布前（gate/快照/apply/simulate，
/// WSL 上 40-70ms）的 abort 全部落空 → join 阻塞到求解自然完成（>40s），
/// 超过测试框架 30s 的 per-case 超时，被 pthread_cancel（async cancel）的
/// 强制 unwind 打进 noexcept 析构 → std::terminate。本 case 用同一重任务 +
/// 提交后立即析构（确定性落在发布窗口内）复现该路径：修复后 dtor 的 abort
/// 轮询在句柄发布后立即取消求解，join ~100ms 内返回（case 秒级 PASS）；
/// 修复前 WSL 上确定性 SIGABRT（exit 134），Windows 上 join 阻塞 30s 后
/// case 被标 TIMEOUT。
TEST_CASE("web_solve_shutdown_race") {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();
    {
        // 同 test_calculator 的重任务载荷：18 个自定义剑魔咒的 dp_merge。
        for (int i = 0; i < 18; ++i) {
            EnchInfo info;
            info.id = NSID("test:e_" + std::to_string(i));
            info.name = "E " + std::to_string(i);
            info.max_level = 5;
            info.multiplier = 1;
            info.supported_items.insert(NSID("#minecraft:swords"));
            expect(ctx.add_enchantment(info), "shutdown seed ench " + std::to_string(i));
        }
        TestApp app(ctx);
        std::string heavy = R"({"target":{"item":"netherite_sword","enchants":[)";
        for (int i = 0; i < 18; ++i) {
            if (i)
                heavy += ",";
            heavy += R"({"id":"test:e_)" + std::to_string(i) + R"(","level":5})";
        }
        heavy += R"(]},"algorithm":"dp_merge"})";
        auto r = app.call(Method::Post, "/api/tasks", heavy);
        expect(r.status == 202, "shutdown-race task submit 202");
        // 立即析构 TestApp：worker 此刻大概率仍在发布窗口内（gate/快照/
        // apply），dtor 必须持续 abort 直至句柄发布并取消求解，join 快速
        // 返回。（本 case 正常完成本身就是断言——修复前 WSL 在此 SIGABRT。）
    }
    TEST_PASS("web_solve_shutdown_race");
}
