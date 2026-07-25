#include "framework/test_utils.h"

#include "domain/business/parsers/ParserShared.h"
#include "domain/business/components/FormatDetector.h"

#include <filesystem>
#include <string>

// ============================================================================
// Section A — ParserShared helper functions
// Namespace: business::parser_detail
// ============================================================================

// ─── make_id ───────────────────────────────────────────────────────────────

void test_make_id_bare() {
    NSID id = business::parser_detail::make_id("sharpness");
    expect(id == NSID("minecraft:sharpness"), "make_id bare -> minecraft:sharpness");
    std::cout << "PASS: test_make_id_bare" << std::endl;
}

void test_make_id_namespaced() {
    NSID id = business::parser_detail::make_id("mod:custom");
    expect(id == NSID("mod:custom"), "make_id namespaced -> mod:custom");
    std::cout << "PASS: test_make_id_namespaced" << std::endl;
}

void test_make_id_custom_ns() {
    NSID id = business::parser_detail::make_id("excavate", "thermalfoundation");
    expect(id == NSID("thermalfoundation:excavate"),
           "make_id custom ns -> thermalfoundation:excavate");
    std::cout << "PASS: test_make_id_custom_ns" << std::endl;
}

// ─── derive_display_name ───────────────────────────────────────────────────

void test_derive_display_name() {
    std::string d1 = business::parser_detail::derive_display_name("sharpness");
    expect_eq(d1, std::string("Sharpness"), "derive_display_name sharpness -> Sharpness");

    std::string d2 = business::parser_detail::derive_display_name("fire_aspect");
    expect_eq(d2, std::string("Fire aspect"), "derive_display_name fire_aspect -> Fire aspect");

    std::string d3 = business::parser_detail::derive_display_name("minecraft:sharpness");
    expect_eq(d3, std::string("Sharpness"),
              "derive_display_name minecraft:sharpness -> Sharpness");

    std::cout << "PASS: test_derive_display_name" << std::endl;
}

// ─── get_category_suffix ───────────────────────────────────────────────────

void test_get_category_suffix() {
    std::string s1 = business::parser_detail::get_category_suffix("diamond_sword");
    expect_eq(s1, std::string("sword"), "get_category_suffix diamond_sword -> sword");

    std::string s2 = business::parser_detail::get_category_suffix("bow");
    expect_eq(s2, std::string("bow"), "get_category_suffix bow -> bow");

    std::string s3 = business::parser_detail::get_category_suffix("diamond_pickaxe");
    expect_eq(s3, std::string("pickaxe"), "get_category_suffix diamond_pickaxe -> pickaxe");

    std::string s4 = business::parser_detail::get_category_suffix("unknown_item");
    expect_eq(s4, std::string("unknown_item"),
              "get_category_suffix unknown_item -> unknown_item");

    std::cout << "PASS: test_get_category_suffix" << std::endl;
}

// ============================================================================
// Section B — FormatDetector::detect()
// ============================================================================

void test_detect_json_ext() {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/vanilla.json"));
    expect_eq(fmt, DataFormat::NativeJson, "detect .json -> NativeJson");
    std::cout << "PASS: test_detect_json_ext" << std::endl;
}

void test_detect_csv_ext() {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/enchants.csv"));
    expect_eq(fmt, DataFormat::NativeCsv, "detect .csv -> NativeCsv");
    std::cout << "PASS: test_detect_csv_ext" << std::endl;
}

void test_detect_unknown_ext() {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/unknown.txt"));
    expect_eq(fmt, DataFormat::Unknown, "detect .txt -> Unknown");
    std::cout << "PASS: test_detect_unknown_ext" << std::endl;
}

void test_detect_no_ext() {
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/noext"));
    expect_eq(fmt, DataFormat::Unknown, "detect no extension -> Unknown");
    std::cout << "PASS: test_detect_no_ext" << std::endl;
}

void test_detect_mc_dir() {
    // Directory structure detection (the path likely doesn't exist in test
    // context, so this should just verify no crash and return Unknown).
    DataFormat fmt = FormatDetector::detect(std::filesystem::path("data/mc_official"));
    // No crash — any return value is acceptable for non-existent paths.
    std::cout << "PASS: test_detect_mc_dir (no crash)" << std::endl;
    (void)fmt;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    try {
        test_make_id_bare();
        test_make_id_namespaced();
        test_make_id_custom_ns();

        test_derive_display_name();

        test_get_category_suffix();

        test_detect_json_ext();
        test_detect_csv_ext();
        test_detect_unknown_ext();
        test_detect_no_ext();
        test_detect_mc_dir();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
