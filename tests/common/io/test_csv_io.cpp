// =============================================================================
// CsvIO tests — parse_string / quoted fields / format_row / write→parse.
// =============================================================================

#define BESQ_TEST_MAIN
#include "common/io/CsvIO.h"
#include "framework/test_framework.h"

#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("test_csv_parse_string") {
    auto table = csv::parse_string("id,name\n1,a\n2,b\n");
    expect(table.size() == 3, "parse_string: header + 2 data rows");
    expect(table[0][0] == "id" && table[0][1] == "name", "parse_string: header row");
    expect(table[1][0] == "1" && table[1][1] == "a", "parse_string: data row 1");
    expect(table[2][0] == "2" && table[2][1] == "b", "parse_string: data row 2");
    TEST_PASS("csv parse_string");
}

TEST_CASE("test_csv_parse_quoted") {
    auto table = csv::parse_string("a,\"b,c\",d\n");
    expect(table.size() == 1 && table[0].size() == 3, "quoted field count");
    expect(table[0][1] == "b,c", "quoted comma preserved inside quotes");
    TEST_PASS("csv quoted fields");
}

TEST_CASE("test_csv_format_row") {
    auto line = csv::format_row({"a", "b,c", "d"});
    expect(line.find("\"b,c\"") != std::string::npos, "comma field is quoted");
    TEST_PASS("csv format_row quoting");
}

TEST_CASE("test_csv_write_roundtrip") {
    auto path = std::filesystem::temp_directory_path() / "besq_csv_test.csv";
    csv::CsvTable table{{"id", "name"}, {"1", "sharpness"}, {"2", "knockback"}};
    csv::write(path, table);
    auto back = csv::parse(path);
    expect(back.size() == 3, "write→parse round-trip: row count");
    expect(back[0][0] == "id" && back[2][1] == "knockback", "write→parse round-trip: content");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    TEST_PASS("csv write/parse round-trip");
}
