// =============================================================================
// Web API tests: BesqContext facade increments (+ per-resource handlers in M1.3+).
// =============================================================================
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/resources/ApiHealth.h"
#include "domain/interface/web/resources/ApiSettings.h"
#include "domain/interface/web/resources/ApiStatus.h"
#include "domain/interface/web/WebHttpError.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "common/io/json.h"
#include "BuildConfig.h"
#include "framework/test_utils.h"
#include <chrono>
#include <string>

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

    LanguageManager::instance().select("en_US");  // restore
    TEST_PASS("ApiSettings");
}

int main() {
    try {
        test_facade_by_name_registry();
        test_api_health();
        test_api_status();
        test_api_settings();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
