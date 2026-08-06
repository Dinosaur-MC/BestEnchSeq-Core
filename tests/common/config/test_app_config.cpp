#include "framework/test_utils.h"
#include "AppConfig.h"
#include "utils/EnvUtil.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

/// RAII guard for the config file under test: removed on construction (start
/// clean) and on scope exit (no artifacts, even when an expect() throws).
struct ConfigFileGuard {
    std::filesystem::path path;
    explicit ConfigFileGuard(std::filesystem::path p) : path(std::move(p)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    ~ConfigFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void write_config(const std::string& content) {
    std::ofstream out(AppConfig::config_file_path());
    out << content;
}

void clear_runtime_env() {
    unset_env("BESQ_LOG_LEVEL");
    unset_env("BESQ_LOG_CONSOLE");
    unset_env("BESQ_LOG_CONSOLE_LEVEL");
    unset_env("BESQ_LANG");
}

void test_default_values() {
    // Unset our test env vars to ensure defaults apply
    clear_runtime_env();
    ConfigFileGuard guard(AppConfig::config_file_path());   // no config.json

    auto cfg = AppConfig::load();

    expect(cfg.memory_mb == 2048, "default memory_mb should be 2048");
    expect(cfg.verbose == false, "default verbose should be false");
    expect(cfg.data_dir == "data/builtin", "default data_dir");
    expect(cfg.log_dir == "logs", "default log_dir");
    expect(cfg.log_level == 0, "default log_level 0");
    expect(cfg.log_console == true, "default log_console true");
    expect(cfg.runtime_lang.empty(), "default runtime_lang empty");
    expect(AppConfig::config_file_path() == "config.json",
           "config file path is <cwd>/config.json");

    std::cout << "  PASS: test_default_values" << std::endl;
}

void test_env_memory_mb() {
    set_env("BESQ_MEMORY_MB", "4096");

    auto cfg = AppConfig::load();

    expect(cfg.memory_mb == 4096, "env BESQ_MEMORY_MB=4096");
    expect(cfg.verbose == false, "verbose still defaults to false");

    unset_env("BESQ_MEMORY_MB");

    std::cout << "  PASS: test_env_memory_mb" << std::endl;
}

void test_env_verbose() {
    set_env("BESQ_VERBOSE", "true");

    auto cfg = AppConfig::load();

    expect(cfg.verbose == true, "env BESQ_VERBOSE=true");

    unset_env("BESQ_VERBOSE");

    std::cout << "  PASS: test_env_verbose" << std::endl;
}

void test_env_data_dir() {
    set_env("BESQ_DATA_DIR", "/custom/data");

    auto cfg = AppConfig::load();

    expect(cfg.data_dir == "/custom/data", "env BESQ_DATA_DIR");

    unset_env("BESQ_DATA_DIR");

    std::cout << "  PASS: test_env_data_dir" << std::endl;
}

void test_config_file_layer() {
    // config.json seeds the runtime-writable fields when env vars are unset
    // (priority: env > config.json > default).
    clear_runtime_env();
    ConfigFileGuard guard(AppConfig::config_file_path());
    write_config(R"({"lang":"zh_CN","log_level":3,"log_console":false,"log_console_level":0})");

    auto cfg = AppConfig::load();
    expect(cfg.log_level == 3, "config.json log_level=3");
    expect(cfg.log_console == false, "config.json log_console=false");
    expect(cfg.log_console_level == 0, "config.json log_console_level=0");
    expect(cfg.runtime_lang == "zh_CN", "config.json lang=zh_CN");
    expect(cfg.memory_mb == 2048, "non-persisted fields keep defaults");

    std::cout << "  PASS: test_config_file_layer" << std::endl;
}

void test_env_overrides_config() {
    clear_runtime_env();
    ConfigFileGuard guard(AppConfig::config_file_path());
    write_config(R"({"lang":"zh_CN","log_level":3,"log_console":false,"log_console_level":0})");
    set_env("BESQ_LOG_LEVEL", "1");
    set_env("BESQ_LOG_CONSOLE", "1");
    set_env("BESQ_LANG", "en_US");

    auto cfg = AppConfig::load();
    expect(cfg.log_level == 1, "env BESQ_LOG_LEVEL overrides config.json");
    expect(cfg.log_console == true, "env BESQ_LOG_CONSOLE overrides config.json");
    expect(cfg.log_console_level == 0, "unset env keeps config.json value");
    expect(cfg.runtime_lang == "en_US", "env BESQ_LANG overrides config.json lang");

    clear_runtime_env();
    std::cout << "  PASS: test_env_overrides_config" << std::endl;
}

void test_corrupt_config_defaults() {
    // A malformed config.json must not crash load() — it falls back to the
    // defaults (LOG_WARN inside AppConfig, no throw).
    clear_runtime_env();
    ConfigFileGuard guard(AppConfig::config_file_path());
    write_config("{not json");

    auto cfg = AppConfig::load();
    expect(cfg.log_level == 0, "corrupt config.json → default log_level");
    expect(cfg.runtime_lang.empty(), "corrupt config.json → empty runtime_lang");

    std::cout << "  PASS: test_corrupt_config_defaults" << std::endl;
}

void test_config_out_of_range_level() {
    // Hand-edited config files may carry out-of-range levels; they must be
    // clamped (0..3) instead of overflowing LogLevel.
    clear_runtime_env();
    ConfigFileGuard guard(AppConfig::config_file_path());
    write_config(R"({"log_level":9,"log_console_level":"x"})");

    auto cfg = AppConfig::load();
    expect(cfg.log_level == 0, "out-of-range log_level clamped to 0");
    expect(cfg.log_console_level == 2, "non-numeric log_console_level → default 2");

    std::cout << "  PASS: test_config_out_of_range_level" << std::endl;
}

void test_save_roundtrip() {
    // save_config_file → the file exists and load_config_file round-trips it.
    ConfigFileGuard guard(AppConfig::config_file_path());
    Json obj = Json::object();
    obj["lang"] = Json("zh_CN");
    obj["log_level"] = Json(int64_t{2});
    obj["log_console"] = Json(false);
    obj["log_console_level"] = Json(int64_t{1});

    expect(AppConfig::save_config_file(obj), "save_config_file returns true");
    expect(std::filesystem::exists(AppConfig::config_file_path()), "config.json written");

    auto loaded = AppConfig::load_config_file();
    expect(loaded.is_valid(), "written config.json parses");
    expect(loaded["lang"].as<std::string>() == "zh_CN", "round-trip lang");
    expect(loaded["log_level"].as<int64_t>() == 2, "round-trip log_level");
    expect(loaded["log_console"].as<bool>() == false, "round-trip log_console");
    expect(loaded["log_console_level"].as<int64_t>() == 1, "round-trip log_console_level");

    std::cout << "  PASS: test_save_roundtrip" << std::endl;
}

void test_save_failure() {
    // Unwritable path (parent dir does not exist) → false, no throw.
    expect(!AppConfig::save_config_file(Json::object(), "no_such_dir_xyz/config.json"),
           "unwritable path returns false");
    // A missing config file yields an empty (non-object) Json — load() treats
    // that as "use the defaults", exactly like an invalid file.
    auto missing = AppConfig::load_config_file("no_such_dir_xyz/config.json");
    expect(missing.type() == JsonType::Empty, "missing config file yields an empty Json");

    std::cout << "  PASS: test_save_failure" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== AppConfig Tests ===" << std::endl;

    try {
        test_default_values();
        test_env_memory_mb();
        test_env_verbose();
        test_env_data_dir();
        test_config_file_layer();
        test_env_overrides_config();
        test_corrupt_config_defaults();
        test_config_out_of_range_level();
        test_save_roundtrip();
        test_save_failure();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
