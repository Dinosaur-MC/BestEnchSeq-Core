// =============================================================================
// Web API tests: BesqContext facade increments (+ per-resource handlers in M1.3+).
// =============================================================================
#include "domain/algorithm/types/AlgorithmState.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/resources/ApiAlgorithm.h"
#include "domain/interface/web/resources/ApiHealth.h"
#include "domain/interface/web/resources/ApiLogs.h"
#include "domain/interface/web/resources/ApiProfiles.h"
#include "domain/interface/web/resources/ApiSettings.h"
#include "domain/interface/web/resources/ApiStatus.h"
#include "domain/interface/web/WebHttpError.h"
#include "domain/interface/web/WebModule.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "common/log/LogRingBuffer.h"
#include "common/io/json.h"
#include "BuildConfig.h"
#include "framework/test_utils.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

// ── Facade increment: by-name profile read/write ──────────────────────────

void test_facade_by_name_registry() {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.fork_profile("builtin:vanilla", "mod:custom");

    // Read a non-active profile's registries without activating it.
    const auto& p = ctx.profile("mod:custom");
    expect(p.ench().contains(NSID("minecraft:sharpness")), "fork inherits enchants");
    expect(ctx.active_profile() == "builtin:vanilla", "read had no activation side effect");

    // Write to a non-active profile by name.
    EnchInfo info;
    info.id = NSID("mod:sword_ench");
    info.name = "Sword Enchantment";
    info.max_level = 5;
    info.multiplier = 2;
    info.supported_items.insert(NSID("#minecraft:swords"));
    expect(ctx.add_enchantment_to("mod:custom", info), "add enchantment by name");
    expect(ctx.profile("mod:custom").ench().contains(NSID("mod:sword_ench")), "ench visible by name");
    expect(!ctx.enchantments().contains(NSID("mod:sword_ench")), "active profile untouched");

    expect(ctx.remove_enchantment_from("mod:custom", NSID("mod:sword_ench")), "remove by name");
    expect(!ctx.profile("mod:custom").ench().contains(NSID("mod:sword_ench")), "ench gone by name");

    // Failure paths: duplicate add → false; unknown profile write → false.
    EnchInfo dup;
    dup.id = NSID("mod:sword_ench");
    dup.name = "Dup";
    dup.max_level = 3;
    dup.multiplier = 1;
    dup.supported_items.insert(NSID("#minecraft:swords"));
    ctx.add_enchantment_to("mod:custom", dup); // (re)add once
    expect(!ctx.add_enchantment_to("mod:custom", dup), "duplicate add by name returns false");
    expect(!ctx.add_enchantment_to("does:not_exist", dup), "add to unknown profile returns false");
    expect(!ctx.remove_enchantment_from("does:not_exist", NSID("mod:sword_ench")), "remove from unknown profile returns false");

    // Unknown profile read must throw std::runtime_error.
    expect_throws_as<std::runtime_error>([&] { ctx.profile("does:not_exist"); }, "unknown profile read throws");
    TEST_PASS("facade by-name registry");
}

// ── Per-resource handlers (M1.3) ─────────────────────────────────────────

void test_api_health() {
    auto json = Json::parse(ApiHealth::handle());
    expect(json["status"].as<std::string>() == "ok", "health status ok");
    expect(json["version"].as<std::string>() == BESQ_VERSION, "health version matches build");
    expect(json["uptime_ms"].as<int64_t>() >= 0, "uptime non-negative");
    TEST_PASS("ApiHealth");
}

void test_api_status() {
    BesqContext ctx;
    ctx.load_builtin();
    auto json = Json::parse(ApiStatus::handle(ctx));
    expect(json["active_profile"].as<std::string>() == "builtin:vanilla", "active profile reported");
    expect(json["algorithm_count"].as<int64_t>() >= 3, "builtin algorithm count");
    expect(json["has_active_solve"] == false, "no active solve when idle");
    TEST_PASS("ApiStatus");
}

void test_api_settings() {
    // Register the builtin zh_CN/en_US tables first — the language flip
    // asserts on the active language, so select("zh_CN") must find it.
    register_builtin_translations(LanguageManager::instance());
    // Registration registers zh_CN first, which leaves zh_CN active by default
    // (first registered wins). Pin the reported default to en_US explicitly.
    LanguageManager::instance().select("en_US");

    BesqContext ctx;
    auto json = Json::parse(ApiSettings::handle_get(ctx));
    expect(json["lang"].as<std::string>() == "en_US", "default lang reported");
    expect(json["gui_host"].as<std::string>() == "127.0.0.1", "gui_host reported");

    // PUT with a valid lang flips the active language.
    auto body = Json::parse(R"({"lang":"zh_CN"})");
    auto out = Json::parse(ApiSettings::handle_put(ctx, body));
    expect(out["lang"].as<std::string>() == "zh_CN", "lang updated in response");
    expect(LanguageManager::instance().active().name() == "zh_CN", "active language flipped");

    // Invalid lang → WebHttpError (WebModule maps its `status` to the wire
    // error envelope in M1.7), language unchanged.
    auto bad = Json::parse(R"({"lang":"xx_XX"})");
    expect_throws_as<webhttp::WebHttpError>(
        [&] { ApiSettings::handle_put(ctx, bad); }, "invalid lang throws");
    expect(LanguageManager::instance().active().name() == "zh_CN", "lang unchanged after bad put");

    // Malformed field types → 400 (JsonException translated), no partial apply.
    expect_throws_as<webhttp::WebHttpError>([&] {
        ApiSettings::handle_put(ctx, Json::parse(R"({"lang":123})"));
    }, "malformed lang type throws 400");
    expect(LanguageManager::instance().active().name() == "zh_CN", "lang unchanged after malformed put");

    // Empty lang is unknown → 400 (no silent no-op).
    expect_throws_as<webhttp::WebHttpError>([&] {
        ApiSettings::handle_put(ctx, Json::parse(R"({"lang":""})"));
    }, "empty lang throws 400");

    // Logger settings write path (GET assert → PUT → GET assert → restore).
    auto lg_before = Json::parse(ApiSettings::handle_get(ctx));
    int32_t old_level = lg_before["log_level"].as<int32_t>();
    bool old_console = lg_before["log_console"].as<bool>();
    int32_t old_console_level = lg_before["log_console_level"].as<int32_t>();

    auto put_lg = Json::parse(ApiSettings::handle_put(ctx, Json::parse(
        R"({"log_level":3,"log_console":false,"log_console_level":0})")));
    expect(put_lg["log_level"].as<int32_t>() == 3, "log_level updated in response");
    expect(put_lg["log_console"].as<bool>() == false, "log_console updated in response");
    expect(put_lg["log_console_level"].as<int32_t>() == 0, "log_console_level updated in response");
    expect(Logger::instance().get_level() == LogLevel::Error, "Logger level actually set");
    expect(!Logger::instance().console_enabled(), "Logger console actually disabled");

    // Invalid range → 400 throw, logger unchanged.
    expect_throws_as<webhttp::WebHttpError>([&] {
        ApiSettings::handle_put(ctx, Json::parse(R"({"log_level":99})"));
    }, "out-of-range log_level throws 400");
    expect(Logger::instance().get_level() == LogLevel::Error, "Logger level unchanged after bad put");

    // Restore original logger settings + language.
    Json restore = Json::object();
    restore["log_level"] = Json(old_level);
    restore["log_console"] = Json(old_console);
    restore["log_console_level"] = Json(old_console_level);
    ApiSettings::handle_put(ctx, restore);
    LanguageManager::instance().select("en_US");
    TEST_PASS("ApiSettings");
}

// ── ApiProfiles (M1.4): profile list/actions + per-name CRUD ──────────────

// Run `fn` and return the WebHttpError status it threw, or -1 when it threw
// nothing / a different exception type.
template <typename F> int thrown_status(F&& fn) {
    try {
        fn();
    } catch (const webhttp::WebHttpError& e) {
        return e.status;
    } catch (...) {
        return -1;
    }
    return -1;
}

void test_api_profiles_error_branches() {
    BesqContext ctx;
    ctx.load_builtin();

    // Unknown action → 400.
    expect(thrown_status([&] {
        ApiProfiles::handle_action(ctx, Json::parse(R"({"action":"nope"})"));
    }) == 400, "unknown action throws 400");

    // Missing action → 400 (JsonException translated).
    expect(thrown_status([&] {
        ApiProfiles::handle_action(ctx, Json::parse(R"({})"));
    }) == 400, "missing action throws 400");

    // Unknown registry kind → 404.
    expect(thrown_status([&] {
        ApiProfiles::handle_read(ctx, "builtin:vanilla", "bogus");
    }) == 404, "unknown kind throws 404");

    // Activate unknown profile → 404 (domain exception mapped, not a 500).
    expect(thrown_status([&] {
        ApiProfiles::handle_action(ctx, Json::parse(
            R"({"action":"activate","name":"does:not_exist"})"));
    }) == 404, "activate unknown profile throws 404");

    // Fork with unknown source → 404.
    expect(thrown_status([&] {
        ApiProfiles::handle_action(ctx, Json::parse(
            R"({"action":"fork","source":"does:not_exist","dest":"mod:x"})"));
    }) == 404, "fork unknown source throws 404");

    // Fork to an existing destination → 409.
    expect(thrown_status([&] {
        ApiProfiles::handle_action(ctx, Json::parse(
            R"({"action":"fork","source":"builtin:vanilla","dest":"builtin:vanilla"})"));
    }) == 409, "fork to existing dest throws 409");

    // Add to an unknown profile → 404.
    expect(thrown_status([&] {
        ApiProfiles::handle_add(ctx, "does:not_exist", "ench", Json::parse(
            R"({"id":"mod:web_ench","name":"Web","max_level":3,"multiplier":1})"));
    }) == 404, "add to unknown profile throws 404");

    // Malformed entry body → 400 (ValidationError translated), no partial apply.
    expect(thrown_status([&] {
        ApiProfiles::handle_add(ctx, "builtin:vanilla", "ench", Json::parse(
            R"({"id":123,"name":"X"})"));
    }) == 400, "malformed add body throws 400");

    // Fork a working scratch profile for the remaining branches.
    auto fork = Json::parse(ApiProfiles::handle_action(ctx, Json::parse(
        R"({"action":"fork","source":"builtin:vanilla","dest":"mod:web2"})")));
    expect(fork["ok"] == true, "fork mod:web2 ok");

    // Duplicate add → 409.
    Json ench = Json::parse(
        R"({"id":"mod:web_ench","name":"Web","max_level":3,"multiplier":1,"supported_items":["#minecraft:swords"]})");
    auto add1 = Json::parse(ApiProfiles::handle_add(ctx, "mod:web2", "ench", ench));
    expect(add1["ok"] == true, "first ench add ok");
    expect(thrown_status([&] {
        ApiProfiles::handle_add(ctx, "mod:web2", "ench", ench);
    }) == 409, "duplicate add throws 409");

    // Entry-not-found remove → 404.
    expect(thrown_status([&] {
        ApiProfiles::handle_remove(ctx, "mod:web2", "ench", "mod:missing");
    }) == 404, "remove missing entry throws 404");

    // Equip round-trip: add → read → remove.
    auto ea = Json::parse(ApiProfiles::handle_add(ctx, "mod:web2", "equip", Json::parse(
        R"({"id":"mod:weapon","name":"W","category":"#minecraft:sword","max_durability":100})")));
    expect(ea["ok"] == true, "add equip ok");
    auto er = Json::parse(ApiProfiles::handle_read(ctx, "mod:web2", "equip"));
    bool equip_found = false;
    for (const auto& e : er["equipments"].as_array())
        if (e["id"].as<std::string>() == "mod:weapon") equip_found = true;
    expect(equip_found, "read returns added equipment");
    auto e_rm = Json::parse(ApiProfiles::handle_remove(ctx, "mod:web2", "equip", "mod:weapon"));
    expect(e_rm["ok"] == true, "remove equip ok");
    expect(!ctx.profile("mod:web2").eq().contains(NSID("mod:weapon")), "equip removed");

    ctx.remove_profile("mod:web2");
    TEST_PASS("ApiProfiles error branches + equip CRUD");
}

void test_api_profiles_list_and_actions() {
    BesqContext ctx;
    ctx.load_builtin();

    // List
    auto list = Json::parse(ApiProfiles::handle_list(ctx));
    expect(list["profiles"][0].as<std::string>() == "builtin:vanilla", "list has builtin");

    // Activate
    auto act = Json::parse(ApiProfiles::handle_action(ctx, Json::parse(
        R"({"action":"activate","name":"builtin:vanilla"})")));
    expect(act["ok"] == true, "activate ok");

    // Fork + activate custom, then add an enchantment to it by name.
    auto fork = Json::parse(ApiProfiles::handle_action(ctx, Json::parse(
        R"({"action":"fork","source":"builtin:vanilla","dest":"mod:web"})")));
    expect(fork["ok"] == true, "fork ok");
    expect(ctx.active_profile() == "builtin:vanilla", "fork does not auto-activate");

    // Add ench to the forked profile via the resource.
    auto add = Json::parse(ApiProfiles::handle_add(ctx, "mod:web", "ench", Json::parse(
        R"({"id":"mod:web_ench","name":"Web","max_level":3,"multiplier":1,"supported_items":["#minecraft:swords"]})")));
    expect(add["ok"] == true, "add ench to named profile ok");
    expect(ctx.profile("mod:web").ench().contains(NSID("mod:web_ench")), "ench present by name");

    // Read it back.
    auto read = Json::parse(ApiProfiles::handle_read(ctx, "mod:web", "ench"));
    bool found = false;
    for (const auto& e : read["enchantments"].as_array())
        if (e["id"].as<std::string>() == "mod:web_ench") found = true;
    expect(found, "read returns the added enchantment");

    // Remove it.
    auto rm = Json::parse(ApiProfiles::handle_remove(ctx, "mod:web", "ench", "mod:web_ench"));
    expect(rm["ok"] == true, "remove ench ok");
    expect(!ctx.profile("mod:web").ench().contains(NSID("mod:web_ench")), "ench removed");

    // Publish to a temp path. Build the JSON body as a Json object (not a
    // string) so Windows backslash paths don't corrupt the JSON escapes.
    auto tmp = std::filesystem::temp_directory_path() / "besq_web_publish.json";
    Json pub_body = Json::object();
    pub_body["action"] = Json("publish");
    pub_body["name"] = Json("mod:web");
    pub_body["version"] = Json("1.0");
    pub_body["tag"] = Json("v1");
    pub_body["path"] = Json(tmp.string());
    auto pub = Json::parse(ApiProfiles::handle_action(ctx, pub_body));
    expect(pub["ok"] == true, "publish ok");
    std::filesystem::remove(tmp);

    // Unknown profile read → 404-ish error envelope.
    expect_throws_as<webhttp::WebHttpError>([&] {
        ApiProfiles::handle_read(ctx, "does:not_exist", "ench");
    }, "unknown profile read throws WebHttpError");

    ctx.remove_profile("mod:web");
    TEST_PASS("ApiProfiles list/actions/CRUD");
}

// ── ApiAlgorithm (M1.5): list/get/load ──────────────────────────────────

void test_api_algorithm() {
    BesqContext ctx;
    ctx.load_builtin();

    auto list = Json::parse(ApiAlgorithm::handle_list(ctx));
    bool has_dp = false;
    for (const auto& a : list["algorithms"].as_array())
        if (a["name"].as<std::string>() == "dp_merge") has_dp = true;
    expect(has_dp, "list includes dp_merge");

    auto one = Json::parse(ApiAlgorithm::handle_get(ctx, "dp_merge"));
    expect(one["name"].as<std::string>() == "dp_merge", "get dp_merge");
    expect(one["mode"].as<std::string>().find("direct") != std::string::npos ||
               one["mode"].as<std::string>().empty(),
           "mode reported");

    expect_throws_as<webhttp::WebHttpError>([&] {
        ApiAlgorithm::handle_get(ctx, "nope");
    }, "unknown algorithm throws WebHttpError");

    auto loaded = Json::parse(ApiAlgorithm::handle_load(ctx, "no/such/dir"));
    expect(loaded["loaded"].as<int64_t>() == 0, "load of missing dir returns 0");
    TEST_PASS("ApiAlgorithm");
}

// ── LogRingBuffer (M1.6): bounded ring buffer + /api/logs ──────────────

void test_log_ring_buffer() {
    auto ring = std::make_shared<LogRingBuffer>(8);
    ring->push(LogLevel::Info, "one");
    ring->push(LogLevel::Error, "two");
    ring->push(LogLevel::Info, "three");

    auto all = ring->snapshot(LogLevel::Debug, 100);
    expect(all.size() == 3, "ring holds 3 entries");

    auto errors_only = ring->snapshot(LogLevel::Warn, 100);
    expect(errors_only.size() == 1 && errors_only[0].message == "two", "level filter works");

    auto tail1 = ring->snapshot(LogLevel::Debug, 1);
    expect(tail1.size() == 1 && tail1[0].message == "three", "tail limit works");

    // Capacity drops the oldest.
    for (int i = 0; i < 12; ++i)
        ring->push(LogLevel::Info, "x" + std::to_string(i));
    auto capped = ring->snapshot(LogLevel::Debug, 100);
    expect(capped.size() == 8, "ring capped at capacity");
    expect(capped[0].message == "x4", "oldest dropped, x4 is oldest");

    ring->clear();
    expect(ring->snapshot(LogLevel::Debug, 100).empty(), "ring cleared");
    TEST_PASS("LogRingBuffer");
}

void test_api_logs() {
    auto ring = std::make_shared<LogRingBuffer>(32);
    ring->push(LogLevel::Info, "hello web logs");
    auto json = Json::parse(ApiLogs::handle(*ring, LogLevel::Debug, 32));
    expect(json["logs"][0]["message"].as<std::string>() == "hello web logs", "log entry served");
    expect(json["logs"][0]["level"].as<std::string>() == "info", "log level serialized");
    expect(json["logs"][0].has("timestamp_ms"), "timestamp_ms present");
    TEST_PASS("ApiLogs");
}

// ── BesqContext::solve_progress (M2.1): live solve state/progress ────────

void test_solve_progress_idle() {
    BesqContext ctx;
    ctx.load_builtin();
    auto prog = ctx.solve_progress();
    expect(prog.state == algorithm::AlgorithmState::Idle, "idle state when nothing runs");
    expect(prog.progress == 0.0, "zero progress when idle");
    TEST_PASS("solve_progress idle");
}

void test_solve_progress_during_run() {
    BesqContext ctx;
    ctx.load_builtin();
    // Seed a large target so bb_dp stays in flight long enough to observe.
    // (bb_dp reports per-layer progress — dp_merge only reports 0/100, so
    // progress > 0.0 is unobservable with dp_merge.)
    for (int i = 0; i < 14; ++i) {
        EnchInfo info;
        info.id = NSID("test:ench_" + std::to_string(i));
        info.name = "Test Ench " + std::to_string(i);
        info.max_level = 5;
        info.multiplier = 1;
        info.supported_items.insert(NSID("#minecraft:swords"));
        expect(ctx.add_enchantment(info), "add test ench");
    }
    EnchSet target_enchs;
    for (int i = 0; i < 14; ++i)
        target_enchs.emplace(NSID("test:ench_" + std::to_string(i)), 5);
    Item target_item;
    target_item.id = NSID("minecraft:diamond_sword");
    target_item.enchantments = target_enchs;
    target_item.durability = 1561;

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{};
    request.algorithm = "bb_dp";
    request.forge_config.platform = MCE::Java;
    request.search_config.max_search_time = std::chrono::milliseconds(1500);

    std::atomic<bool> done{false};
    std::thread solver([&] {
        ctx.solve(request);
        done = true;
    });

    bool saw_running = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        auto p = ctx.solve_progress();
        if (p.state == algorithm::AlgorithmState::Running && p.progress > 0.0)
            saw_running = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    solver.join();
    expect(saw_running, "observed a Running state with progress while solving");
    TEST_PASS("solve_progress during run");
}

// ── WebModule (M1.7): route table + dispatch + error envelopes ───────────

void test_web_module_routing() {
    BesqContext ctx;
    ctx.load_builtin();

    webhttp::WebModule module(ctx);
    module.set_static_resources({
        {"/index.html", {"text/html", "<html>hi</html>"}},
        {"/app.js", {"text/javascript", "console.log(1);"}},
    });

    // /health
    auto h = Json::parse(module.dispatch("GET", "/health", "").body);
    expect(h["status"].as<std::string>() == "ok", "module routes /health");

    // /api/status
    auto s = Json::parse(module.dispatch("GET", "/api/status", "").body);
    expect(s["active_profile"].as<std::string>() == "builtin:vanilla", "module routes /api/status");

    // /api/profile list
    auto p = Json::parse(module.dispatch("GET", "/api/profile", "").body);
    expect(p["profiles"][0].as<std::string>() == "builtin:vanilla", "module routes /api/profile");

    // Static resource
    auto st = module.dispatch("GET", "/app.js", "");
    expect(st.status == 200, "static resource served");
    expect(st.body == "console.log(1);", "static resource body");

    // Root → index.html
    auto root = module.dispatch("GET", "/", "");
    expect(root.body == "<html>hi</html>", "root serves index.html");

    // Unknown API → 404 JSON
    auto nf = Json::parse(module.dispatch("GET", "/api/does_not_exist", "").body);
    expect(nf["ok"] == false, "unknown api path is error envelope");

    // Handler exception (unknown profile) → 404 envelope
    auto bad = Json::parse(module.dispatch("GET", "/api/profile/nope/ench", "").body);
    expect(bad["ok"] == false, "unknown profile gives error envelope");

    // POST /api/profile action
    auto act = Json::parse(module.dispatch("POST", "/api/profile",
        R"({"action":"fork","source":"builtin:vanilla","dest":"mod:routed"})").body);
    expect(act["ok"] == true, "module dispatches profile action");

    // Route with {id} param: read the forked profile's ench registry.
    auto read = Json::parse(module.dispatch("GET", "/api/profile/mod:routed/ench", "").body);
    expect(read["enchantments"].type() == JsonType::Array, "param route resolves");

    ctx.remove_profile("mod:routed");
    TEST_PASS("WebModule routing");
}

int main() {
    try {
        test_facade_by_name_registry();
        test_api_health();
        test_api_status();
        test_api_settings();
        test_api_profiles_list_and_actions();
        test_api_profiles_error_branches();
        test_api_algorithm();
        test_log_ring_buffer();
        test_api_logs();
        test_solve_progress_idle();
        test_solve_progress_during_run();
        test_web_module_routing();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
