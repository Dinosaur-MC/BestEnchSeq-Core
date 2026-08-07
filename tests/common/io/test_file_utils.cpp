// test_file_utils.cpp — file_utils::list_directory (non-recursive listing,
// directories-first ordering, size capture, missing-dir tolerance).
#define BESQ_TEST_MAIN
#include "common/io/FileUtils.hpp"
#include "framework/test_framework.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("test_file_utils") {
    // Scratch tree under the test cwd (cleaned on exit; stale dirs from a
    // crashed run are removed first so the entry count is deterministic).
    const fs::path base = fs::current_path() / "besq_file_utils_test_dir";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "sub", ec);
    fs::create_directories(base / "zdir", ec);
    {
        std::ofstream(base / "b.txt") << "hello";
        std::ofstream(base / "a.txt") << "12345";   // 5 bytes
    }

    // ── listing: 4 entries, directories first, then name-sorted files ──
    auto entries = file_utils::list_directory(base);
    expect(entries.size() == 4, "four entries listed");
    if (entries.size() == 4) {
        expect(entries[0].is_dir && entries[0].name == "sub",
               "entry 0 = sub directory first");
        expect(entries[1].is_dir && entries[1].name == "zdir",
               "entry 1 = zdir directory second");
        expect(!entries[2].is_dir && entries[2].name == "a.txt",
               "entry 2 = a.txt after directories");
        expect(!entries[3].is_dir && entries[3].name == "b.txt",
               "entry 3 = b.txt name-sorted");
        expect(entries[2].size == 5, "a.txt size captured (5 bytes)");
        expect(entries[0].size == 0, "directory size 0");
    }

    // ── non-recursive: the nested dir is a single entry, its file is not ──
    auto sub = file_utils::list_directory(base / "sub");
    expect(sub.empty(), "nested sub directory lists empty (non-recursive)");

    // ── missing / non-directory paths → empty, never throws ──
    expect(file_utils::list_directory(base / "nope").empty(),
           "missing directory yields empty list");
    expect(file_utils::list_directory(base / "a.txt").empty(),
           "a file yields empty list");

    fs::remove_all(base, ec);
    TEST_PASS("test_file_utils");
}
