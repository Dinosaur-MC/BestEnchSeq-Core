#pragma once

#include "utils/EnvUtil.hpp"
#include <cstdint>
#include <string>

/// Application-level configuration backed by environment variables.
///
/// Values are loaded from environment variables at construction time, with
/// sensible hard-coded defaults.  This is the bottom configuration layer;
/// CLI arguments (CLIConfig) override these at a higher level.
///
/// Environment variable reference:
///   BESQ_MEMORY_MB     — Memory budget in MB for A* search      (default: 2048)
///   BESQ_VERBOSE       — Enable verbose diagnostic output       (default: false)
///   BESQ_DATA_DIR      — Built-in data directory path            (default: "data/builtin")
///   BESQ_LOG_DIR       — Log output directory                    (default: "logs")
struct AppConfig {
    int64_t  memory_mb       = 2048;
    bool     verbose         = false;
    std::string data_dir     = "data/builtin";
    std::string log_dir      = "logs";

    /// Load configuration from environment variables, applying defaults
    /// for any variables that are not set.
    static AppConfig load() noexcept {
        AppConfig cfg;
        cfg.memory_mb = get_env<int64_t>("BESQ_MEMORY_MB",    cfg.memory_mb);
        cfg.verbose   = get_env<bool>   ("BESQ_VERBOSE",      cfg.verbose);
        cfg.data_dir  = get_env<std::string>("BESQ_DATA_DIR", cfg.data_dir);
        cfg.log_dir   = get_env<std::string>("BESQ_LOG_DIR",  cfg.log_dir);
        return cfg;
    }
};
