#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "utils/ExeDir.hpp"

#include <filesystem>
#include <string>

TEST_CASE("test_exe_dir_resolves") {
    const auto dir = exe_dir();
    expect(!dir.empty(), "exe_dir() must be non-empty");
    if (dir.empty()) {
        TEST_PASS("test_exe_dir_resolves (skipped — empty exe_dir)");
        return;
    }
    expect(std::filesystem::is_directory(dir), "exe_dir() must be a directory");

    // The test binary itself must live in exe_dir(): the runtime default
    // paths (profiles/logs/states/config.json) resolve against this directory.
    std::string exe_name = "test_exe_dir";
#ifdef _WIN32
    exe_name += ".exe";
#endif
    expect(std::filesystem::exists(dir / exe_name),
           "exe_dir() must contain the test binary (test_exe_dir)");

    // Cached: repeated calls return the same path.
    expect(exe_dir() == dir, "exe_dir() must be stable across calls");

    std::cout << "  PASS: test_exe_dir_resolves (" << dir.string() << ")" << std::endl;
}
