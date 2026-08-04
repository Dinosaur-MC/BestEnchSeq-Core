#pragma once

#include "common/log/LogTypes.h"
#include "common/utils/EnvUtil.hpp"
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
///   BESQ_LOG_CONSOLE     — Mirror logs to console: Warn/Error→stderr, Debug/Info→stdout (default: 1; keep level ≥2 with --format json/compact)
///   BESQ_LOG_CONSOLE_LEVEL — Console mirror threshold (0=debug,1=info,2=warn,3=error, default: 2)
///   BESQ_SANDBOX         — Run algorithm plugins in a sandboxed worker (default: 0)
///   BESQ_WORKER_PATH     — Path to besq-worker binary (default: auto — <exe_dir>/besq-worker[.exe], then PATH)
///   BESQ_GUI_HOST        — GUI HTTP server bind address (default: "127.0.0.1")
///   BESQ_GUI_PORT        — GUI HTTP server port; 0 = auto-assign a free ephemeral port (default: 0)
///   BESQ_GUI_OPEN_BROWSER— Open the default browser instead of the WebView2 window
///                          (dev mode / when WebView2 runtime is unavailable; default: 0)
struct AppConfig {
    int64_t  memory_mb       = 2048;
    bool     verbose         = false;
    std::string data_dir     = "data/builtin";
    std::string algo_dir     = "algorithms";   // <exe_dir>/algorithms/
    std::string log_dir      = "logs";
    int32_t  log_level       = 0;     // 0=Debug, 1=Info, 2=Warn, 3=Error
    size_t   log_retention   = 5;
    bool     log_console       = true;  // mirror to console (stderr/stdout)
    int32_t  log_console_level = 2;     // console threshold (0=Debug..3=Error)
    bool     sandbox_enabled   = false; // run plugins in a sandboxed worker
    std::string sandbox_worker_path;    // besq-worker binary ("" → <exe_dir>/besq-worker[.exe], then PATH)
    std::string gui_host = "127.0.0.1"; // GUI HTTP server bind address
    uint16_t    gui_port = 0;           // GUI HTTP server port (0 = auto-assign free port)
    bool        gui_open_browser = false; // dev: open default browser instead of WebView2 window

    /// Global app configuration singleton — loads BESQ_* env vars on first
    /// use.  Consumers include AppConfig.h and read directly (no param
    /// threading); main.cpp configures the Logger from the same instance.
    static AppConfig& get() noexcept {
        static AppConfig cfg = load();
        return cfg;
    }

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
        cfg.log_console       = get_env<bool>   ("BESQ_LOG_CONSOLE",       cfg.log_console);
        cfg.log_console_level = get_env<int32_t>("BESQ_LOG_CONSOLE_LEVEL", cfg.log_console_level);
        cfg.sandbox_enabled   = get_env<bool>   ("BESQ_SANDBOX",       cfg.sandbox_enabled);
        cfg.sandbox_worker_path = get_env_str   ("BESQ_WORKER_PATH");
        cfg.gui_host         = get_env<std::string>("BESQ_GUI_HOST",           cfg.gui_host);
        cfg.gui_port         = get_env<uint16_t>   ("BESQ_GUI_PORT",           cfg.gui_port);
        cfg.gui_open_browser = get_env<bool>       ("BESQ_GUI_OPEN_BROWSER",   cfg.gui_open_browser);
        return cfg;
    }

    /// Produce the Logger's typed config from this AppConfig.
    LoggerConfig logger_config() const noexcept {
        return {log_level, log_retention, log_console, log_console_level};
    }
};
