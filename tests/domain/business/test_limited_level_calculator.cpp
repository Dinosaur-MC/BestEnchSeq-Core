#define BESQ_TEST_MAIN
#include "builtin/ItemProperties.h"
#include "common/CommonTypes.h"
#include "common/io/json.h"
#include "domain/business/components/LimitedLevelCalculator.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Profile.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "framework/test_framework.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::unordered_set<NSID> tag_set(const std::string& s) {
    return {NSID(s)};
}

// ─── Test: compute from min_cost formula ────────────────────────────────
// enchant A: max_level=5, min_cost_base=1, min_cost_per_level=11, supported
// `#minecraft:swords` → diamond_sword (enchantability 10).
//   power = round((30+1+2·⌊10/4⌋)·1.15) = round((31+4)·1.15) = round(40.25) = 40
//   level = (40−1)/11 + 1 = 39/11 + 1 = 3 + 1 = 4, clamped to max_level 5 → 4.

TEST_CASE("test_ll_compute_from_min_cost") {
    TagResolver resolver;
    resolver.load_tag_content("minecraft:swords", R"({"values": ["minecraft:diamond_sword"]})");

    EnchantmentRegistry reg;
    reg.insert(
        EnchInfo{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 0, 1, false, {}, tag_set("#minecraft:swords"), 1, 11});

    std::unordered_map<std::string, ItemProperty> props;
    props["diamond_sword"].enchantability = 10;

    LimitedLevelCalculator::compute(reg, resolver, props);

    expect_eq(reg.at(NSID("minecraft:sharpness")).limited_level, 4, "computed limited_level from min_cost formula");

    TEST_PASS("test_ll_compute_from_min_cost");
}

// ─── Test: treasure → 0 (highest priority, even with min_cost) ──────────

TEST_CASE("test_ll_treasure") {
    TagResolver resolver;
    resolver.load_tag_content("minecraft:swords", R"({"values": ["minecraft:diamond_sword"]})");

    EnchantmentRegistry reg;
    reg.insert(EnchInfo{NSID("minecraft:mending"), "Mending", MCE::All, 1, 1, 4, true, {}, tag_set("#minecraft:swords"), 2, 5});

    LimitedLevelCalculator::compute(reg, resolver, {});

    expect_eq(reg.at(NSID("minecraft:mending")).limited_level, 0, "treasure enchant → limited_level 0");

    TEST_PASS("test_ll_treasure");
}

// ─── Test: fallback — no min_cost but provided hint keeps stored value ──

TEST_CASE("test_ll_keep_provided") {
    EnchantmentRegistry reg;
    EnchInfo ench{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 4, 1, false, {}, tag_set("#minecraft:swords")};
    ench.limited_level = 4;
    ench.limited_level_provided = true;
    reg.insert(ench);

    LimitedLevelCalculator::compute(reg, TagResolver(), {});

    expect_eq(reg.at(NSID("minecraft:sharpness")).limited_level, 4, "legacy provided hint keeps stored value");

    TEST_PASS("test_ll_keep_provided");
}

// ─── Test: fallback — no min_cost, not provided → max_level ─────────────

TEST_CASE("test_ll_fallback_max_level") {
    EnchantmentRegistry reg;
    reg.insert(EnchInfo{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 0, 1, false, {}, tag_set("#minecraft:swords")});

    LimitedLevelCalculator::compute(reg, TagResolver(), {});

    expect_eq(reg.at(NSID("minecraft:sharpness")).limited_level, 5, "no min_cost + not provided → max_level");

    TEST_PASS("test_ll_fallback_max_level");
}

// ─── Test: min_cost present but no contributing item → conservative 1 ───
// A mod item without item_props contributes nothing → limited_level = 1.

TEST_CASE("test_ll_no_contributing_item") {
    TagResolver resolver;
    resolver.load_tag_content("mypack:staffs", R"({"values": ["mypack:magic_staff"]})");

    EnchantmentRegistry reg;
    reg.insert(
        EnchInfo{NSID("mypack:staff_power"), "Staff Power", MCE::All, 4, 0, 3, false, {}, tag_set("#mypack:staffs"), 8, 6});

    std::unordered_map<std::string, ItemProperty> props; // vanilla only — no magic_staff

    LimitedLevelCalculator::compute(reg, resolver, props);

    expect_eq(reg.at(NSID("mypack:staff_power")).limited_level, 1, "no contributing item → conservative 1");

    TEST_PASS("test_ll_no_contributing_item");
}

// ─── Test: EnchSerializer JSON mirror — min_cost round-trips, no bogus
//    limited_level when the hint is absent (B-T18 roundtrip fix). ────────

TEST_CASE("test_ll_serializer_json_roundtrip") {
    TagRegistry tag_reg;
    EnchInfo info{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 0, 1, false, {}, tag_set("#minecraft:swords"), 1, 11};

    std::string json_str = EnchSerializer::to_json({info}, tag_reg);
    Json json = Json::parse(json_str);
    expect(json.is_valid(), "serialized JSON parses");
    expect(json.has("enchantments"), "serialized JSON has enchantments array");

    const auto& arr = json["enchantments"].as_array();
    expect(arr.size() == 1, "one enchantment serialized");
    const auto& obj = arr[0].as<Json::Object>();

    // min_cost carried; limited_level NOT emitted (hint false).
    expect(obj.find("min_cost_base") != obj.end(), "min_cost_base emitted");
    expect(obj.at("min_cost_base").as<int64_t>() == 1, "min_cost_base value");
    expect(obj.find("min_cost_per_level") != obj.end(), "min_cost_per_level emitted");
    expect(obj.at("min_cost_per_level").as<int64_t>() == 11, "min_cost_per_level value");
    expect(obj.find("limited_level") == obj.end(), "no limited_level emitted when hint absent");

    // Reimport keeps the cost data (and no bogus hint).
    EnchInfo i2;
    i2.from_json(Json(obj));
    expect(i2.min_cost_base == 1, "reimport min_cost_base");
    expect(i2.min_cost_per_level == 11, "reimport min_cost_per_level");
    expect(i2.limited_level_provided == false, "reimport keeps hint false");

    TEST_PASS("test_ll_serializer_json_roundtrip");
}

// ─── Test: EnchSerializer JSON emits limited_level when hint present ─────

TEST_CASE("test_ll_serializer_json_hint") {
    TagRegistry tag_reg;
    EnchInfo info{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 4, 1, false, {}, tag_set("#minecraft:swords")};
    info.limited_level = 4;
    info.limited_level_provided = true;

    std::string json_str = EnchSerializer::to_json({info}, tag_reg);
    Json json = Json::parse(json_str);
    const auto& obj = json["enchantments"].as_array()[0].as<Json::Object>();
    expect(obj.find("limited_level") != obj.end(), "limited_level emitted when hint present");
    expect(obj.at("limited_level").as<int64_t>() == 4, "limited_level value");

    TEST_PASS("test_ll_serializer_json_hint");
}

// ─── Test: EnchSerializer CSV export carries the min_cost columns (B-T18) ──

TEST_CASE("test_ll_serializer_csv_columns") {
    TagRegistry tag_reg;
    EnchInfo info{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 0, 1, false, {}, tag_set("#minecraft:swords"), 10, 7};

    std::string csv_str = EnchSerializer::to_csv({info}, tag_reg);
    expect(csv_str.find("min_cost_base") != std::string::npos,
           "CSV header carries min_cost_base");
    expect(csv_str.find("min_cost_per_level") != std::string::npos,
           "CSV header carries min_cost_per_level");
    // The cost values appear in the data row.
    expect(csv_str.find(",10,") != std::string::npos, "CSV row carries min_cost_base value");
    expect(csv_str.find(",7,") != std::string::npos, "CSV row carries min_cost_per_level value");

    TEST_PASS("test_ll_serializer_csv_columns");
}

// ─── Test: builtin vanilla.json loads with computed limited_level (B-T19) ─
// The regenerated vanilla.json carries `min_cost` + `is_treasure` but NO
// pre-computed `limited_level`; the registry-level calculator must back-fill it
// at load time: sharpness → computed > 0; mending → treasure → 0.

TEST_CASE("test_ll_builtin_vanilla_data") {
    ProfileLoader loader;
    Profile p = loader.load_builtin();

    const auto& sharp = p.ench().at(NSID("minecraft:sharpness"));
    expect(sharp.min_cost_base > 0, "sharpness: min_cost_base carried from vanilla.json");
    expect(sharp.min_cost_per_level > 0, "sharpness: min_cost_per_level carried");
    expect(!sharp.is_treasure, "sharpness: not treasure");
    expect(sharp.limited_level > 0,
           "sharpness: COMPUTED limited_level > 0 (no longer a stored field)");

    const auto& mending = p.ench().at(NSID("minecraft:mending"));
    expect(mending.is_treasure, "mending: is_treasure from vanilla.json");
    expect_eq(mending.limited_level, 0, "mending: treasure → limited_level 0");

    TEST_PASS("test_ll_builtin_vanilla_data");
}

} // namespace
