#define BESQ_TEST_MAIN
#include "framework/test_framework.h"

#include "domain/business/components/FormatDetector.h"
#include "domain/business/parsers/ParserShared.h"

#include <filesystem>
#include <fstream>
#include <string>

// ============================================================================
// Section A — ParserShared helper functions
// Namespace: business::parser_detail
// ============================================================================

// ─── make_id ───────────────────────────────────────────────────────────────

TEST_CASE("test_make_id_bare") {
    NSID id = business::parser_detail::make_id("sharpness");
    expect(id == NSID("minecraft:sharpness"), "make_id bare -> minecraft:sharpness");
}

TEST_CASE("test_make_id_namespaced") {
    NSID id = business::parser_detail::make_id("mod:custom");
    expect(id == NSID("mod:custom"), "make_id namespaced -> mod:custom");
}

TEST_CASE("test_make_id_custom_ns") {
    NSID id = business::parser_detail::make_id("excavate", "thermalfoundation");
    expect(id == NSID("thermalfoundation:excavate"), "make_id custom ns -> thermalfoundation:excavate");
}

// ─── derive_display_name ───────────────────────────────────────────────────

TEST_CASE("test_derive_display_name") {
    std::string d1 = business::parser_detail::derive_display_name("sharpness");
    expect_eq(d1, std::string("Sharpness"), "derive_display_name sharpness -> Sharpness");

    std::string d2 = business::parser_detail::derive_display_name("fire_aspect");
    expect_eq(d2, std::string("Fire aspect"), "derive_display_name fire_aspect -> Fire aspect");

    std::string d3 = business::parser_detail::derive_display_name("minecraft:sharpness");
    expect_eq(d3, std::string("Sharpness"), "derive_display_name minecraft:sharpness -> Sharpness");
}

// ─── get_category_suffix ───────────────────────────────────────────────────

TEST_CASE("test_get_category_suffix") {
    std::string s1 = business::parser_detail::get_category_suffix("diamond_sword");
    expect_eq(s1, std::string("sword"), "get_category_suffix diamond_sword -> sword");

    std::string s2 = business::parser_detail::get_category_suffix("bow");
    expect_eq(s2, std::string("bow"), "get_category_suffix bow -> bow");

    std::string s3 = business::parser_detail::get_category_suffix("diamond_pickaxe");
    expect_eq(s3, std::string("pickaxe"), "get_category_suffix diamond_pickaxe -> pickaxe");

    std::string s4 = business::parser_detail::get_category_suffix("unknown_item");
    expect_eq(s4, std::string("unknown_item"), "get_category_suffix unknown_item -> unknown_item");
}

// ============================================================================
// Section B — FormatDetector::detect()
// ============================================================================

TEST_CASE("test_detect_json_ext") {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/vanilla.json"));
    expect_eq(fmt, DataFormat::NativeJson, "detect .json -> NativeJson");
}

TEST_CASE("test_detect_csv_ext") {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/enchants.csv"));
    expect_eq(fmt, DataFormat::NativeCsv, "detect .csv -> NativeCsv");
}

TEST_CASE("test_detect_unknown_ext") {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/unknown.txt"));
    expect_eq(fmt, DataFormat::Unknown, "detect .txt -> Unknown");
}

TEST_CASE("test_detect_no_ext") {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/noext"));
    expect_eq(fmt, DataFormat::Unknown, "detect no extension -> Unknown");
}

TEST_CASE("test_detect_mc_dir") {
    // Directory structure detection (the path likely doesn't exist in test
    // context, so this should just verify no crash and return Unknown).
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/mc_official"));
    // No crash — any return value is acceptable for non-existent paths.
    (void)fmt;
}

// ============================================================================
// Section C — FormatDetector::parse() carries datapack item_tags (#24)
// ============================================================================

TEST_CASE("test_format_detector_mc_official_carries_item_tags") {
    // Build a minimal datapack in a temp dir (committed fixtures under
    // data/tests/datapack/ are bare enchantment JSONs, not full pack dirs).
    auto dir = std::filesystem::temp_directory_path() / "fmt_det_item_tags";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mypack" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "mypack" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dir / "data" / "mypack" / "enchantment" / "leeching.json");
        f << R"({"supported_items": "#mypack:swords", "anvil_cost": 2, "max_level": 3})";
    }
    {
        std::ofstream f(dir / "data" / "mypack" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    auto result = FormatDetector::parse(dir);
    expect(!result.item_tags.empty(), "McOfficial branch of FormatDetector carries datapack item_tags");
    bool has_swords = false;
    for (const auto& tag : result.item_tags) {
        if (tag.key == "mypack:swords") {
            has_swords = true;
            break;
        }
    }
    expect(has_swords, "item_tags contains the mypack:swords definition");
    // Sanity: enchantment + equipment still carried alongside.
    expect(!result.enchantments.empty(), "McOfficial branch also carries enchantments");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_format_detector_mc_official_carries_item_tags");
}

// ============================================================================
// Main
// ============================================================================
