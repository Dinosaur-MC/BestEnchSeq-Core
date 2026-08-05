// =============================================================================
// Web API tests (modern controllers): Health/Status/Settings/Profiles over
// web::Router. Algorithm/Calculator/Logs controllers arrive in later tasks and
// extend this file.
// =============================================================================
#include "domain/interface/web/controllers/HealthController.h"
#include "domain/interface/web/controllers/StatusController.h"
#include "domain/interface/web/controllers/SettingsController.h"
#include "domain/interface/web/controllers/ProfilesController.h"
#include "domain/interface/web/controllers/AlgorithmController.h"
#include "domain/interface/web/controllers/CalculatorController.h"
#include "domain/interface/web/controllers/LogsController.h"
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/SseHub.h"
#include "domain/interface/web/WebModule.h"
#include "domain/interface/components/http/StreamChannel.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/BesqContext.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "common/log/LogRingBuffer.h"
#include "framework/test_utils.h"
#include <chrono>
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
        router.register_controller<StatusController>(c);
        router.register_controller<SettingsController>(c);
        router.register_controller<ProfilesController>(ctx, gate);
        router.register_controller<AlgorithmController>(c, *solve);
        router.register_controller<CalculatorController>(*solve, hub);
        router.register_controller<LogsController>(c, hub);
    }
    HttpResponse call(Method m, std::string path, std::string body = "") {
        // Mirror HttpParser: split path from the ?query=... string (real requests
        // arrive with req.query already parsed).
        HttpRequest req;
        req.method = m;
        auto q = path.find('?');
        req.path = q == std::string::npos ? std::move(path) : path.substr(0, q);
        if (q != std::string::npos) req.query = parse_query(path.substr(q + 1));
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
    std::string key = "builtin:vanilla";      // the guaranteed root profile
    expect(app.ctx.profile_exists(key), "root profile present");

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

    // Empty dest in create/fork → 400 (was a 500 before the guard).
    auto emptyd = app.call(Method::Post, "/api/profiles",
                           R"({"source":")" + key + R"(","dest":""})");
    expect(emptyd.status == 400 && emptyd.body.find("INVALID_FIELD") != std::string::npos,
           "create empty dest 400 INVALID_FIELD");
    auto emptyf = app.call(Method::Post, "/api/profiles/" + key + "/fork",
                           R"({"dest":""})");
    expect(emptyf.status == 400 && emptyf.body.find("INVALID_FIELD") != std::string::npos,
           "fork empty dest 400 INVALID_FIELD");

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

void test_algorithms(TestApp& app) {
    // list → 200 array of names containing a builtin strategy.
    auto l = app.call(Method::Get, "/api/algorithms");
    expect(l.status == 200 && l.body.find("dp_merge") != std::string::npos,
           "algorithms list 200 contains dp_merge");

    // detail → every AlgorithmDetail field serialized.
    auto d = app.call(Method::Get, "/api/algorithms/dp_merge");
    expect(d.status == 200, "algorithm detail 200");
    for (const char* f : {"name", "version", "origin", "supported_mode",
                          "is_resumable", "plugin_path", "has_audit"})
        expect(d.body.find(f) != std::string::npos, std::string("detail field ") + f);

    // Unloading a builtin (trusted kernel) is rejected → 400 UNLOAD_REJECTED.
    // No solve is active at this point, so the gate (409 TASK_ACTIVE) is clear.
    auto un = app.call(Method::Post, "/api/algorithms/unload", R"({"name":"dp_merge"})");
    expect(un.status == 400 && un.body.find("UNLOAD_REJECTED") != std::string::npos,
           "unload builtin 400 UNLOAD_REJECTED");

    // Unknown algorithm → 404.
    auto no = app.call(Method::Get, "/api/algorithms/nope");
    expect(no.status == 404, "unknown algorithm 404");

    // Load with a missing/invalid body → 400 INVALID_FIELD (code present).
    auto load_empty = app.call(Method::Post, "/api/algorithms/load", "{}");
    expect(load_empty.status == 400 && load_empty.body.find("code") != std::string::npos,
           "algorithms load {} 400 with code");

    // Load from a nonexistent directory → 200 {"loaded":0} (scan does not throw).
    auto load_missing = app.call(Method::Post, "/api/algorithms/load",
                                 R"({"dir":"/nonexistent_dir_xyz"})");
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
    expect(light.header_value("Location").find("/api/tasks/") != std::string::npos,
           "submit Location header");
    std::string light_id = lb["task_id"].as<std::string>();

    // Poll the light task to a terminal state (bounded ≤5s) so cancel/status on
    // a completed task below are deterministic.
    bool light_done = false;
    for (int i = 0; i < 50 && !light_done; ++i) {
        auto st = app.call(Method::Get, "/api/tasks/" + light_id);
        if (st.status != 200) break;
        auto sj = Json::parse(st.body);
        light_done = sj["state"].as<std::string>() != "running";
        if (!light_done) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(light_done, "light task reached terminal state");

    // DELETE unknown → 404; DELETE a completed task → 200 no-op.
    auto cno = app.call(Method::Delete, "/api/tasks/nope");
    expect(cno.status == 404 && cno.body.find("code") != std::string::npos,
           "cancel unknown task 404");
    auto cdone = app.call(Method::Delete, "/api/tasks/" + light_id);
    expect(cdone.status == 200 && cdone.body.find("ok") != std::string::npos,
           "cancel completed task 200 no-op");

    // Status of the completed light task → 200 with a result payload.
    auto sdone = app.call(Method::Get, "/api/tasks/" + light_id);
    expect(sdone.status == 200 && sdone.body.find("result") != std::string::npos,
           "status completed task carries result");

    // Submit with a missing required field ("target") → 400 INVALID_TASK.
    auto badtask = app.call(Method::Post, "/api/tasks", "{}");
    expect(badtask.status == 400 && badtask.body.find("code") != std::string::npos,
           "submit {} 400 with code");

    // test_profiles deletes netherite_sword from the builtin profile; restore it
    // so the heavy target resolves (idempotent: DELETE then POST).
    (void)app.call(Method::Delete,
                   "/api/profiles/builtin:vanilla/equipments/minecraft:netherite_sword");
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

    std::string heavy = R"({"target":{"item":"netherite_sword","enchants":[)";
    for (int i = 0; i < 18; ++i) {
        if (i) heavy += ",";
        heavy += R"({"id":"test:e_)" + std::to_string(i) + R"(","level":5})";
    }
    heavy += R"(]},"algorithm":"dp_merge"})";

    auto heavy_resp = app.call(Method::Post, "/api/tasks", heavy);
    expect(heavy_resp.status == 202, "heavy task submit 202");
    auto hb = Json::parse(heavy_resp.body);
    std::string heavy_id = hb["task_id"].as<std::string>();

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
    expect(heav_ev.status == 200 && heav_ev.is_stream &&
               heav_ev.content_type == "text/event-stream",
           "heavy events stream response");
    expect(app.hub.subscriber_count(heavy_id) == 1,
           "heavy task SSE subscription registered");

    // Unknown task → 404 for both status and events.
    auto no = app.call(Method::Get, "/api/tasks/nope");
    expect(no.status == 404, "unknown task status 404");
    auto noev = app.call(Method::Get, "/api/tasks/nope/events");
    expect(noev.status == 404, "unknown task events 404");

    // The heavy task may still be running; it is cancelled + joined when the
    // TestApp (owning WebSolveService) is destroyed at the end of main().
}

void test_logs(TestApp& app) {
    // The test never installs a ring buffer, so the incremental tail is empty
    // but well-formed: {"logs":[],"next":0}.
    auto l = app.call(Method::Get, "/api/logs?limit=5");
    expect(l.status == 200 && l.body.find("logs") != std::string::npos,
           "logs tail 200 contains logs");
    expect(l.body.find("next") != std::string::npos, "logs tail carries next cursor");

    // Non-numeric limit → 400 INVALID_FIELD.
    auto bad = app.call(Method::Get, "/api/logs?limit=x");
    expect(bad.status == 400 && bad.body.find("INVALID_FIELD") != std::string::npos,
           "invalid limit 400 INVALID_FIELD");

    // events endpoint → stream response (frame delivery is the transport task).
    auto ev = app.call(Method::Get, "/api/logs/events");
    expect(ev.status == 200 && ev.is_stream &&
               ev.content_type == "text/event-stream",
           "logs events stream response");

    // ── Fix 3 additions ──

    // Exact empty-ring shape: no ring installed → {"logs":[],"next":0}. If a
    // ring is present in the test environment, derive the expected cursor from
    // its current contents instead (deterministic either way).
    auto empty = app.call(Method::Get, "/api/logs");
    expect(empty.status == 200, "logs empty tail 200");
    auto ej = Json::parse(empty.body);
    expect(ej["logs"].type() == JsonType::Array && ej["logs"].as_array().empty(),
           "logs exact empty array");
    int64_t expected_next = 0;
    if (auto ring = Logger::instance().ring_buffer()) {
        auto snap = ring->snapshot(LogLevel::Debug, 200);
        if (!snap.empty()) expected_next = snap.back().timestamp_ms;
    }
    expect(ej["next"].as<int64_t>() == expected_next, "logs exact next cursor");

    // Overflow / negative limit → 400.
    auto ovf = app.call(Method::Get, "/api/logs?limit=99999999999999999999");
    expect(ovf.status == 400 && ovf.body.find("code") != std::string::npos,
           "limit overflow 400");
    auto neg = app.call(Method::Get, "/api/logs?limit=-1");
    expect(neg.status == 400 && neg.body.find("code") != std::string::npos,
           "limit negative 400");

    // Explicit limit=0 → empty slice (cursor stays put), not a full dump.
    auto zero = app.call(Method::Get, "/api/logs?limit=0");
    expect(zero.status == 200, "limit=0 tail 200");
    auto zj = Json::parse(zero.body);
    expect(zj["logs"].type() == JsonType::Array && zj["logs"].as_array().empty(),
           "limit=0 exact empty array");
    expect(zj["next"].as<int64_t>() == 0, "limit=0 next 0");
}

// ── Fake StreamChannel: captures every frame delivered to the "connection" ──
struct FakeChannel : web::StreamChannel {
    std::vector<std::string> frames;
    std::function<void()> on_close_cb;
    bool close_fired = false;
    void post_frame(std::string f) override { frames.push_back(std::move(f)); }
    void on_close(std::function<void()> cb) override { on_close_cb = std::move(cb); }
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

    app.hub.publish(id, "data: x\n\n");
    bool delivered = false;
    for (const auto& f : fake->frames)
        if (f == "data: x\n\n") delivered = true;
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
void test_hub_clear(TestApp& app) {
    auto fake = std::make_shared<FakeChannel>();
    LogsController lc(app.ctx, app.hub);
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/api/logs/events";
    req.stream = fake;
    auto r = lc.events(req);
    expect(r.status == 200 && r.is_stream, "logs events stream response via channel");
    expect(app.hub.subscriber_count("logs") == 1, "logs subscription registered before clear");

    app.hub.clear();
    expect(app.hub.subscriber_count("logs") == 0, "hub cleared all subscriptions");

    app.hub.publish("logs", "data: nope\n\n");
    bool leaked = false;
    for (const auto& f : fake->frames)
        if (f == "data: nope\n\n") leaked = true;
    expect(!leaked, "no frame delivered after clear");
}

/// 连接关闭钩子测试（确定性证明 on_close → 退订 的接线）：
/// 用 "logs" 键最确定 —— 该键无任何自动退订（WebSolveService 只清任务键），
/// 唯一能把它从 1 清零的机制就是 LogsController 注册的 on_close 回调。
void test_stream_close_hook(TestApp& app) {
    auto fake = std::make_shared<FakeChannel>();
    LogsController lc(app.ctx, app.hub);
    HttpRequest req;
    req.method = Method::Get;
    req.path = "/api/logs/events";
    req.stream = fake;
    auto r = lc.events(req);
    expect(r.status == 200 && r.is_stream, "logs events stream response via channel");
    expect(app.hub.subscriber_count("logs") == 1, "logs subscription registered");

    // 关闭前帧投递链路正常。
    app.hub.publish("logs", "data: hi\n\n");
    bool got = false;
    for (const auto& f : fake->frames)
        if (f == "data: hi\n\n") got = true;
    expect(got, "frame delivered before close");

    // 模拟客户端断开 → on_close 回调 → 退订（唯一清零路径，证明接线生效）。
    expect(fake->on_close_cb != nullptr, "on_close callback registered on logs channel");
    fake->fire_close();
    expect(app.hub.subscriber_count("logs") == 0, "logs subscription dropped after close");

    // 退订后 publish 不再送达该通道（死连接回调已从 hub 移除）。
    app.hub.publish("logs", "data: nope\n\n");
    bool leaked = false;
    for (const auto& f : fake->frames)
        if (f == "data: nope\n\n") leaked = true;
    expect(!leaked, "no frame delivered after close");
}
} // namespace

int main() {
    BesqContext ctx; ctx.load_builtin(); ctx.load_profiles();
    TestApp app(ctx);
    try {
        test_health(app);
        test_status(app);
        test_settings(app);
        test_profiles(app);
        test_algorithms(app);
        test_stream_channel(app);   // 须在 test_calculator 之前（单活动槽）
        test_stream_close_hook(app); // on_close → 退订 接线测试（依赖 logs 键未被 test_logs 污染）
        test_hub_clear(app);        // SseHub::clear() 清空全部订阅
        test_calculator(app);
        test_logs(app);
        test_web_module(app.ctx);
        TEST_PASS("test_web_api");
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
