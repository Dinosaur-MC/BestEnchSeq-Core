#include "utils/ParserUtils.h"
#include "io/CsvIO.h"
#include "framework/test_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fs::path create_temp_file(const fs::path &dir, const std::string &name,
                          const std::string &content) {
    fs::path path = dir / name;
    std::ofstream ofs(path);
    ofs << content;
    return path;
}

// ---------------------------------------------------------------------------
// Format detection
// ---------------------------------------------------------------------------

void test_format_detection() {
    expect(ParserUtils::detect_format("data.json") == ParserUtils::DataFormat::NativeJSON,
           "json file detection");
    expect(ParserUtils::detect_format("data.csv") == ParserUtils::DataFormat::NativeCSV,
           "csv file detection");
    expect(ParserUtils::detect_format("data.xyz") == ParserUtils::DataFormat::Unknown,
           "unknown extension");
    expect(ParserUtils::detect_format("DATA.JSON") == ParserUtils::DataFormat::NativeJSON,
           "case insensitive json");
    expect(ParserUtils::detect_format("file.Xyz") == ParserUtils::DataFormat::Unknown,
           "unknown extension uppercase");
    expect(ParserUtils::detect_format("") == ParserUtils::DataFormat::Unknown,
           "empty path");
    expect(ParserUtils::detect_format("noextension") == ParserUtils::DataFormat::Unknown,
           "path without extension");

    std::cout << "  [OK] test_format_detection" << std::endl;
}

// ---------------------------------------------------------------------------
// CSV line parsing
// ---------------------------------------------------------------------------

void test_csv_line_parsing() {
    // Basic
    auto fields = csv::split_line("a,b,c");
    expect(fields.size() == 3, "basic csv size");
    expect(fields[0] == "a", "first field basic");
    expect(fields[1] == "b", "second field basic");
    expect(fields[2] == "c", "third field basic");

    // Quoted with comma
    fields = csv::split_line("a,b,\"c,d\",e");
    expect(fields.size() == 4, "csv with quoted field size");
    expect(fields[0] == "a", "first field quoted");
    expect(fields[2] == "c,d", "quoted field with comma");

    // Escaped quotes inside quoted field
    fields = csv::split_line("\"a\"\"b\"");
    expect(fields.size() == 1, "escaped quotes size");
    expect(fields[0] == "a\"b", "escaped quotes content");

    // Empty fields
    fields = csv::split_line("a,,c");
    expect(fields.size() == 3, "empty field size");
    expect(fields[0] == "a", "first before empty");
    expect(fields[1] == "", "empty field");
    expect(fields[2] == "c", "last after empty");

    // Empty line
    fields = csv::split_line("");
    expect(fields.size() == 0, "empty line returns empty vector");

    // Single field
    fields = csv::split_line("hello");
    expect(fields.size() == 1, "single field size");
    expect(fields[0] == "hello", "single field content");

    // Trailing comma (empty last field)
    fields = csv::split_line("a,");
    expect(fields.size() == 2, "trailing comma size");
    expect(fields[0] == "a", "first with trailing comma");
    expect(fields[1] == "", "empty trailing field");

    // Leading comma (empty first field)
    fields = csv::split_line(",b");
    expect(fields.size() == 2, "leading comma size");
    expect(fields[0] == "", "empty leading field");
    expect(fields[1] == "b", "second with leading comma");

    // Multiple quotes in sequence not at boundaries
    fields = csv::split_line("\"x\",\"y\"");
    expect(fields.size() == 2, "multiple quoted fields size");
    expect(fields[0] == "x", "first quoted field");
    expect(fields[1] == "y", "second quoted field");

    // Escaped double-quote adjacent to delimiter
    fields = csv::split_line("\"\"\"\",x");
    // This is: """," -> first field is """, x is second
    // Inside quotes: we see "" (escaped quote -> "), then " (end quote), then , (delimiter)
    // Wait, actually: """," means:
    // " - start quote
    // " - escaped or end?
    // The next char is ", so it's "" (escaped -> literal ")
    // " - now this is end quote
    // , - delimiter
    // So first field is '"'
    expect(fields.size() == 2, "adjacent escaped and closing quote size");
    expect(fields[0] == "\"", "adjacent escaped and closing quote content");
    expect(fields[1] == "x", "second field after tricky quoting");

    std::cout << "  [OK] test_csv_line_parsing" << std::endl;
}

// ---------------------------------------------------------------------------
// Namespace helpers
// ---------------------------------------------------------------------------

void test_namespace_helpers() {
    // split_namespace
    auto [ns, id] = ParserUtils::split_namespace("minecraft:sharpness");
    expect(ns == "minecraft", "namespace extracted");
    expect(id == "sharpness", "id extracted");

    auto [ns2, id2] = ParserUtils::split_namespace("sharpness");
    expect(ns2.empty(), "no namespace = empty string");
    expect(id2 == "sharpness", "whole string is id");

    auto [ns3, id3] = ParserUtils::split_namespace(":");
    expect(ns3.empty(), "empty namespace before colon");
    expect(id3.empty(), "empty id after colon");

    auto [ns4, id4] = ParserUtils::split_namespace("mod:item:extra");
    expect(ns4 == "mod", "namespace from multi-colon");
    expect(id4 == "item:extra", "rest after first colon");

    auto [ns5, id5] = ParserUtils::split_namespace(":leading");
    expect(ns5.empty(), "empty namespace with leading colon");
    expect(id5 == "leading", "id with leading colon");

    // qualify_id
    expect(ParserUtils::qualify_id("sharpness") == "minecraft:sharpness",
           "default ns added");
    expect(ParserUtils::qualify_id("thermalfoundation:excavate") == "thermalfoundation:excavate",
           "already qualified");
    expect(ParserUtils::qualify_id("minecraft:sharpness") == "minecraft:sharpness",
           "minecraft prefix unchanged");
    expect(ParserUtils::qualify_id("", "minecraft") == "minecraft:",
           "empty id with default ns");
    expect(ParserUtils::qualify_id("foo", "custom") == "custom:foo",
           "custom default namespace");

    std::cout << "  [OK] test_namespace_helpers" << std::endl;
}

// ---------------------------------------------------------------------------
// MC official structure detection
// ---------------------------------------------------------------------------

void test_mc_official_structure() {
    fs::path tmp = fs::temp_directory_path() / "parser_test_mc_struct";

    // Clean up from previous runs
    fs::remove_all(tmp);

    // Valid MC structure with enchantment/
    fs::create_directories(tmp / "data" / "minecraft" / "enchantment");
    expect(ParserUtils::is_mc_official_structure(tmp),
           "MC structure with enchantment dir");
    expect(ParserUtils::detect_format(tmp) == ParserUtils::DataFormat::MCOfficial,
           "detect_format returns MCOfficial for MC directory");
    expect(ParserUtils::detect_mc_official(tmp) == ParserUtils::DataFormat::MCOfficial,
           "detect_mc_official returns MCOfficial for MC directory");
    fs::remove_all(tmp);

    // Valid MC structure with tags/
    fs::create_directories(tmp / "data" / "minecraft" / "tags");
    expect(ParserUtils::is_mc_official_structure(tmp),
           "MC structure with tags dir");
    fs::remove_all(tmp);

    // Non-MC directory
    fs::create_directories(tmp / "random" / "stuff");
    expect(!ParserUtils::is_mc_official_structure(tmp),
           "non-MC directory rejected");
    expect(ParserUtils::detect_format(tmp) == ParserUtils::DataFormat::Unknown,
           "detect_format returns Unknown for non-MC directory");
    fs::remove_all(tmp);

    // Non-existent directory
    expect(!ParserUtils::is_mc_official_structure(tmp),
           "non-existent directory rejected");

    // File path (not directory)
    fs::create_directories(tmp);
    auto file_path = create_temp_file(tmp, "test.txt", "hello");
    expect(!ParserUtils::is_mc_official_structure(file_path),
           "file path rejected");
    fs::remove_all(tmp);

    // data dir exists but its contents are not namespace dirs
    fs::create_directories(tmp / "data" / "minecraft");
    // minecraft is a dir but has no enchantment/ or tags/ subdirs
    expect(!ParserUtils::is_mc_official_structure(tmp),
           "data dir without enchantment/tags subdirs rejected");
    fs::remove_all(tmp);

    std::cout << "  [OK] test_mc_official_structure" << std::endl;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

void test_file_io() {
    fs::path tmp = fs::temp_directory_path() / "parser_test_file_io";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // Create files for find_files testing (no .txt files yet)
    create_temp_file(tmp, "a.json", "{}");
    create_temp_file(tmp, "b.json", "{}");
    create_temp_file(tmp, "c.csv", "a,b");
    fs::create_directories(tmp / "sub");
    create_temp_file(tmp / "sub", "d.json", "{}");

    // find_files - recursive
    auto json_files = ParserUtils::find_files(tmp, ".json");
    expect(json_files.size() == 3, "found 3 json files recursively");

    auto csv_files = ParserUtils::find_files(tmp, ".csv");
    expect(csv_files.size() == 1, "found 1 csv file");

    auto txt_files = ParserUtils::find_files(tmp, ".txt");
    expect(txt_files.size() == 0, "no txt files");

    // find_files with extension without leading dot
    auto json_files2 = ParserUtils::find_files(tmp, "json");
    expect(json_files2.size() == 3, "extension without dot normalized");

    // find_files with empty extension (return all)
    auto all_files = ParserUtils::find_files(tmp, "");
    expect(all_files.size() == 4, "empty extension returns all files");

    // find_files with non-existent directory
    auto no_files = ParserUtils::find_files(tmp / "nonexistent", ".json");
    expect(no_files.size() == 0, "no files in non-existent dir");

    // find_files with a file path (not a directory)
    auto existing_file = create_temp_file(tmp, "dummy.txt", "dummy");
    no_files = ParserUtils::find_files(existing_file, ".json");
    expect(no_files.size() == 0, "no files when path is a file not dir");
    fs::remove(existing_file);

    // read_file
    auto file_path = create_temp_file(tmp, "test.txt", "Hello, World!");
    auto content = ParserUtils::read_file(file_path);
    expect(content == "Hello, World!", "read_file content");

    // read_file with non-existent file
    try {
        ParserUtils::read_file(tmp / "nonexistent.txt");
        expect(false, "should throw for non-existent file");
    } catch (const std::runtime_error &) {
        expect(true, "threw for non-existent file");
    }

    fs::remove_all(tmp);

    std::cout << "  [OK] test_file_io" << std::endl;
}

// ---------------------------------------------------------------------------
// Full CSV parsing (file-level)
// ---------------------------------------------------------------------------

void test_parse_csv() {
    fs::path tmp = fs::temp_directory_path() / "parser_test_csv";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::string csv_content =
        "name,level,cost\n"
        "sharpness,5,10\n"
        "protection,4,8\n";

    auto file_path = create_temp_file(tmp, "test.csv", csv_content);

    auto rows = csv::parse(file_path);
    expect(rows.size() == 3, "csv has 3 rows including header");
    expect(rows[0].size() == 3, "header has 3 fields");
    expect(rows[0][0] == "name", "header first field");
    expect(rows[1][0] == "sharpness", "first data row first field");
    expect(rows[2][2] == "8", "last data row last field");

    // CSV with quoted fields
    std::string csv_quoted =
        "id,description\n"
        "1,\"has a comma, inside\"\n"
        "2,\"has \"\"quotes\"\" inside\"\n";

    auto file_quoted = create_temp_file(tmp, "quoted.csv", csv_quoted);
    auto rows2 = csv::parse(file_quoted);
    expect(rows2.size() == 3, "quoted csv has 3 rows");
    expect(rows2[1][1] == "has a comma, inside",
           "quoted field with comma");
    expect(rows2[2][1] == "has \"quotes\" inside",
           "escaped quotes inside quoted field");

    fs::remove_all(tmp);

    std::cout << "  [OK] test_parse_csv" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== ParserUtils Tests ===" << std::endl;
    try {
        test_format_detection();
        test_csv_line_parsing();
        test_namespace_helpers();
        test_mc_official_structure();
        test_file_io();
        test_parse_csv();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
