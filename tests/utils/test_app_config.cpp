#include "framework/test_utils.h"
#include "types/AppConfig.h"
#include "utils/EnvUtil.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void test_default_values() {
    // Unset our test env vars to ensure defaults apply
    unset_env("BESQ_MEMORY_MB");
    unset_env("BESQ_VERBOSE");
    unset_env("BESQ_DATA_DIR");

    auto cfg = AppConfig::load();

    expect(cfg.memory_mb == 2048, "default memory_mb should be 2048");
    expect(cfg.verbose == false, "default verbose should be false");
    expect(cfg.data_dir == "data/builtin", "default data_dir");
    expect(cfg.log_dir == "logs", "default log_dir");

    std::cout << "  [OK] test_default_values" << std::endl;
}

void test_env_memory_mb() {
    set_env("BESQ_MEMORY_MB", "4096");

    auto cfg = AppConfig::load();

    expect(cfg.memory_mb == 4096, "env BESQ_MEMORY_MB=4096");
    expect(cfg.verbose == false, "verbose still defaults to false");

    unset_env("BESQ_MEMORY_MB");

    std::cout << "  [OK] test_env_memory_mb" << std::endl;
}

void test_env_verbose() {
    set_env("BESQ_VERBOSE", "true");

    auto cfg = AppConfig::load();

    expect(cfg.verbose == true, "env BESQ_VERBOSE=true");

    unset_env("BESQ_VERBOSE");

    std::cout << "  [OK] test_env_verbose" << std::endl;
}

void test_env_data_dir() {
    set_env("BESQ_DATA_DIR", "/custom/data");

    auto cfg = AppConfig::load();

    expect(cfg.data_dir == "/custom/data", "env BESQ_DATA_DIR");

    unset_env("BESQ_DATA_DIR");

    std::cout << "  [OK] test_env_data_dir" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== AppConfig Tests ===" << std::endl;

    try {
        test_default_values();
        test_env_memory_mb();
        test_env_verbose();
        test_env_data_dir();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
