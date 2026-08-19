#pragma once

#include "common/io/json.h"
#include "common/log/log.hpp"
#include "common/log/LogTypes.h"
#include "common/utils/EnvUtil.hpp"
#include "common/utils/ExeDir.hpp"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

/// Application-level configuration backed by environment variables.
///
/// Values are loaded at construction time with the following precedence
/// (highest first):
///   1. Environment variables (BESQ_*)
///   2. <exe_dir>/config.json — the runtime-persisted settings file written
///      by PATCH /api/settings (lang / log_level / log_console /
///      log_console_level / log_retention)
///   3. Hard-coded defaults
///
/// Runtime default paths (config.json / logs / algorithms / states /
/// profiles) all resolve against the executable's own directory (exe_dir()),
/// so behavior is independent of the process CWD.  <exe_dir> is assumed
/// writable (local tooling); env vars still win so a command-line override
/// always beats the file.  Every consumer of AppConfig::load() (CLI + HTTP
/// service) applies the same precedence.
///
/// Environment variable reference:
///   BESQ_MEMORY_MB       — Memory budget in MB for A* search          (default: 2048)
///   BESQ_VERBOSE         — Enable verbose diagnostic output           (default: false)
///   BESQ_ALGO_DIR        — Algorithm plugin directory path             (default: <exe_dir>/algorithms/)
///   BESQ_PROFILES_DIR    — Profiles directory (default: <exe_dir>/profiles)
///   BESQ_LOG_DIR         — Log output directory                        (default: <exe_dir>/logs)
///   BESQ_LOG_LEVEL       — Minimum log level (0=debug,1=info,2=warn,3=error, default: 0)
///   BESQ_LOG_RETENTION   — Max historic log files to keep during rotation (default: 5)
///   BESQ_LOG_CONSOLE     — Mirror logs to console: Warn/Error→stderr, Debug/Info→stdout (default: 1; keep level ≥2 with --format json/compact)
///   BESQ_LOG_CONSOLE_LEVEL — Console mirror threshold (0=debug,1=info,2=warn,3=error, default: 2)
///   BESQ_SANDBOX         — Run algorithm plugins in a sandboxed worker (default: 0)
///   BESQ_WORKER_PATH     — Path to besq-worker binary (default: auto — <exe_dir>/besq-worker[.exe], then PATH)
///   BESQ_STATE_DIR       — Algorithm checkpoint directory               (default: <exe_dir>/states)
///   BESQ_STATE_AUTOSAVE  — Auto-save a checkpoint when a solve is paused (default: 0)
///   BESQ_LANG_DIR        — On-disk language files directory             (default: <exe_dir>/langs)
///   BESQ_HTTP_HOST       — HTTP service bind address (default: "127.0.0.1")
///   BESQ_HTTP_PORT       — HTTP service port; 0 = auto-assign a free ephemeral port (default: 0)
///   BESQ_HTTP_WORKERS    — HTTP service consumer threads (default: 2)
///   BESQ_HTTP_RES_DIR    — /public disk root; "" → not mounted (default: none)
///   BESQ_LANG            — Language code for the service (default: config.json lang, else en_US)
struct AppConfig {
    int64_t  memory_mb       = 2048;
    bool     verbose         = false;
    std::string algo_dir     = (exe_dir() / "algorithms").string(); // <exe_dir>/algorithms/
    std::string profiles_dir;               // "" = 默认 <exe_dir>/profiles（profile set_dir 持久化）
    std::string log_dir      = (exe_dir() / "logs").string();       // <exe_dir>/logs
    int32_t  log_level       = 0;     // 0=Debug, 1=Info, 2=Warn, 3=Error
    size_t   log_retention   = 5;
    bool     log_console       = true;  // mirror to console (stderr/stdout)
    int32_t  log_console_level = 2;     // console threshold (0=Debug..3=Error)
    bool     sandbox_enabled   = false; // run plugins in a sandboxed worker
    std::string sandbox_worker_path;    // besq-worker binary ("" → <exe_dir>/besq-worker[.exe], then PATH)
    std::string state_dir     = (exe_dir() / "states").string();    // <exe_dir>/states
    bool     state_autosave   = false;  // save a checkpoint automatically on solve pause
    std::string langs_dir     = (exe_dir() / "langs").string();    // <exe_dir>/langs
    std::string http_host = "127.0.0.1"; // HTTP service bind address
    uint16_t    http_port = 0;           // HTTP service port (0 = auto-assign free port)
    size_t      http_workers = 2;        // HTTP service consumer threads
    std::string http_res_dir;            // /public disk root ("" → not mounted)
    std::string runtime_lang;            // language from config.json / BESQ_LANG (the service applies it at startup)

    /// Global app configuration singleton — loads BESQ_* env vars on first
    /// use.  Consumers include AppConfig.h and read directly (no param
    /// threading); main.cpp configures the Logger from the same instance.
    static AppConfig& get() noexcept {
        static AppConfig cfg = load();
        return cfg;
    }

    /// Path of the runtime-persisted config file (<exe_dir>/config.json).
    /// Falls back to "config.json" (CWD) when exe_dir() is unavailable.
    static std::string config_file_path() noexcept {
        const auto dir = exe_dir();
        return dir.empty() ? std::string("config.json") : (dir / "config.json").string();
    }

    /// Load configuration from environment variables, applying defaults
    /// for any variables that are not set.  Precedence: env > config.json
    /// > default (the config file is read first; env vars override it
    /// per-field below).
    static AppConfig load() noexcept {
        AppConfig cfg;

        // ── config.json layer (lowest of the three) ──
        // A missing file is silent; a corrupt one (or one carrying garbage
        // field types / out-of-range levels) is ignored per-field with a
        // LOG_WARN and falls back to the defaults.
        const Json file = load_config_file();
        if (file.is_valid() && file.type() == JsonType::Object) {
            if (file.has("lang") && file["lang"].type() == JsonType::String)
                cfg.runtime_lang = file["lang"].as<std::string>();
            if (file.has("log_level"))
                cfg.log_level = checked_level(file["log_level"], "log_level",
                                              cfg.log_level);
            if (file.has("log_console") && file["log_console"].type() == JsonType::Bool)
                cfg.log_console = file["log_console"].as<bool>();
            if (file.has("log_console_level"))
                cfg.log_console_level = checked_level(file["log_console_level"],
                                                      "log_console_level",
                                                      cfg.log_console_level);
            if (file.has("log_retention"))
                cfg.log_retention = checked_retention(file["log_retention"], cfg.log_retention);
            if (file.has("profiles_dir") && file["profiles_dir"].type() == JsonType::String)
                cfg.profiles_dir = file["profiles_dir"].as<std::string>();
            if (file.has("algo_dir") && file["algo_dir"].type() == JsonType::String)
                cfg.algo_dir = file["algo_dir"].as<std::string>();
        }

        // ── env layer (overrides config.json per-field) ──
        cfg.memory_mb     = get_env<int64_t> ("BESQ_MEMORY_MB",     cfg.memory_mb);
        cfg.verbose       = get_env<bool>    ("BESQ_VERBOSE",       cfg.verbose);
        cfg.algo_dir      = get_env<std::string>("BESQ_ALGO_DIR",  cfg.algo_dir);
        cfg.profiles_dir  = get_env<std::string>("BESQ_PROFILES_DIR", cfg.profiles_dir);
        cfg.log_dir       = get_env<std::string>("BESQ_LOG_DIR",   cfg.log_dir);
        cfg.log_level     = get_env<int32_t>  ("BESQ_LOG_LEVEL",    cfg.log_level);
        cfg.log_retention = get_env<size_t>   ("BESQ_LOG_RETENTION", cfg.log_retention);
        cfg.log_console       = get_env<bool>   ("BESQ_LOG_CONSOLE",       cfg.log_console);
        cfg.log_console_level = get_env<int32_t>("BESQ_LOG_CONSOLE_LEVEL", cfg.log_console_level);
        cfg.sandbox_enabled   = get_env<bool>   ("BESQ_SANDBOX",       cfg.sandbox_enabled);
        cfg.sandbox_worker_path = get_env_str   ("BESQ_WORKER_PATH");
        cfg.state_dir         = get_env<std::string>("BESQ_STATE_DIR",  cfg.state_dir);
        cfg.state_autosave    = get_env<bool>   ("BESQ_STATE_AUTOSAVE", cfg.state_autosave);
        cfg.langs_dir         = get_env<std::string>("BESQ_LANG_DIR", cfg.langs_dir);
        cfg.http_host         = get_env<std::string>("BESQ_HTTP_HOST",    cfg.http_host);
        cfg.http_port         = get_env<uint16_t>   ("BESQ_HTTP_PORT",    cfg.http_port);
        cfg.http_workers      = get_env<size_t>     ("BESQ_HTTP_WORKERS", cfg.http_workers);
        cfg.http_res_dir      = get_env_str         ("BESQ_HTTP_RES_DIR");
        const std::string env_lang = get_env_str("BESQ_LANG");
        if (!env_lang.empty())
            cfg.runtime_lang = env_lang;    // env beats config.json for lang too
        return cfg;
    }

    /// Read `path` (default <cwd>/config.json) and parse it.  Returns the
    /// parsed Json, or an invalid Json when the file is missing (silent —
    /// the default layer applies) or corrupt (LOG_WARN + invalid).
    static Json load_config_file(const std::string& path = config_file_path()) noexcept {
        Json empty;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return empty;
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        std::string error;
        Json parsed = Json::parse(content, error);
        if (!parsed.is_valid()) {
            LOG_WARN("%s ignored (invalid JSON: %s)", path.c_str(), error.c_str());
            return empty;
        }
        return parsed;
    }

    /// Best-effort persist `obj` to `path` (default <cwd>/config.json).
    /// Returns false (with LOG_WARN) on write failure — callers keep the
    /// in-memory state regardless (persistence is best-effort).
    static bool save_config_file(const Json& obj,
                                 const std::string& path = config_file_path()) noexcept {
        try {
            std::ofstream out(path, std::ios::trunc);
            if (!out) {
                LOG_WARN("failed to write %s", path.c_str());
                return false;
            }
            out << obj.to_string();
            return static_cast<bool>(out);
        } catch (const std::exception& e) {
            LOG_WARN("failed to write %s: %s", path.c_str(), e.what());
            return false;
        }
    }

    /// Produce the Logger's typed config from this AppConfig.
    LoggerConfig logger_config() const noexcept {
        return {log_level, log_retention, log_console, log_console_level};
    }

private:
    /// Bound-check a JSON level value (0..3) before it enters the Logger:
    /// hand-edited config files may carry out-of-range numbers that would
    /// otherwise overflow LogLevel.  Out-of-range / non-numeric → the
    /// field's own default (`def`) with a LOG_WARN.
    static int32_t checked_level(const Json& v, const char* field,
                                 int32_t def) noexcept {
        try {
            int64_t lv = v.as<int64_t>();
            if (lv >= 0 && lv <= 3)
                return static_cast<int32_t>(lv);
            LOG_WARN("config.json %s out of range (%lld), using default %d",
                     field, static_cast<long long>(lv), def);
        } catch (const JsonException&) {
            LOG_WARN("config.json %s is not a number, using default %d", field, def);
        }
        return def;
    }

    /// Bound-check a JSON retention value before it enters the Logger:
    /// negative would wrap into size_t (rotation then deletes every historic
    /// run).  Negative / non-numeric → the field's own default (`def`) with a
    /// LOG_WARN.
    static size_t checked_retention(const Json& v, size_t def) noexcept {
        try {
            int64_t rv = v.as<int64_t>();
            if (rv >= 0)
                return static_cast<size_t>(rv);
            LOG_WARN("config.json log_retention negative (%lld), using default %zu",
                     static_cast<long long>(rv), def);
        } catch (const JsonException&) {
            LOG_WARN("config.json log_retention is not a number, using default %zu", def);
        }
        return def;
    }
};
