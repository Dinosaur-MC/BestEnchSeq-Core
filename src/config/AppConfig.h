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
///   BESQ_MEMORY_MB       — Memory budget in MB for A* search          (default: 2048)
///   BESQ_VERBOSE         — Enable verbose diagnostic output           (default: false)
///   BESQ_DATA_DIR        — Built-in data directory path                (default: "data/builtin")
///   BESQ_ALGO_DIR        — Algorithm plugin directory path             (default: auto — <exe_dir>/algorithms/)
///   BESQ_LOG_DIR         — Log output directory                        (default: "logs")
///   BESQ_LOG_LEVEL       — Minimum log level (0=debug,1=info,2=warn,3=error, default: 0)
///   BESQ_LOG_RETENTION   — Max historic log files to keep during rotation (default: 5)
struct AppConfig {
    int64_t  memory_mb       = 2048;
    bool     verbose         = false;
    std::string data_dir     = "data/builtin";
    std::string algo_dir     = "algorithms";   // <exe_dir>/algorithms/
    std::string log_dir      = "logs";
    int32_t  log_level       = 0;     // 0=Debug, 1=Info, 2=Warn, 3=Error
    size_t   log_retention   = 5;

    /// Load configuration from environment variables, applying defaults
    /// for any variables that are not set.
    static AppConfig load() noexcept {
        AppConfig cfg;
        cfg.memory_mb     = get_env<int64_t> ("BESQ_MEMORY_MB",     cfg.memory_mb);
        cfg.verbose       = get_env<bool>    ("BESQ_VERBOSE",       cfg.verbose);
        cfg.data_dir      = get_env<std::string>("BESQ_DATA_DIR",  cfg.data_dir);
        cfg.algo_dir      = get_env<std::string>("BESQ_ALGO_DIR",  cfg.algo_dir);
        cfg.log_dir       = get_env<std::string>("BESQ_LOG_DIR",   cfg.log_dir);
        cfg.log_level     = get_env<int32_t>  ("BESQ_LOG_LEVEL",    cfg.log_level);
        cfg.log_retention = get_env<size_t>   ("BESQ_LOG_RETENTION", cfg.log_retention);
        return cfg;
    }
};
