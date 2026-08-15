#include "SettingsController.h"
#include "AppConfig.h"
#include "common/i18n/Language.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "domain/interface/components/http/Router.h"

namespace web {

namespace {

/// Current settings snapshot — mirrors the old ApiSettings::handle_get
/// (field names/shape unchanged; machine format, not localized).  The
/// writable set is lang / log_level / log_console / log_console_level /
/// log_retention; the gui_* / memory_mb / sandbox_enabled and the path
/// fields (log_dir/algo_dir/state_dir) are read-only (set at startup — the
/// server is already bound).
///
/// `effective_port` is the actually bound port (WebModule injects it after
/// server.start(); 0 when not injected, e.g. tests driving the Router
/// directly).  gui_port prefers it — the configured 0 means "OS auto-assign".
Json build_settings_json(uint16_t effective_port) {
    const auto& cfg = AppConfig::get();
    Json o = Json::object();
    o["lang"] = Json(std::string(LanguageManager::instance().active().name()));
    o["gui_host"] = Json(cfg.gui_host);
    o["gui_port"] = Json(static_cast<int64_t>(effective_port != 0 ? effective_port : cfg.gui_port));
    o["gui_open_browser"] = Json(cfg.gui_open_browser);
    o["gui_workers"] = Json(static_cast<int64_t>(cfg.gui_workers));
    o["memory_mb"] = Json(cfg.memory_mb);
    o["sandbox_enabled"] = Json(cfg.sandbox_enabled);
    o["log_level"] = Json(static_cast<int64_t>(Logger::instance().get_level()));
    o["log_retention"] = Json(static_cast<int64_t>(Logger::instance().get_retention()));
    o["log_console"] = Json(Logger::instance().console_enabled());
    o["log_console_level"] = Json(static_cast<int64_t>(Logger::instance().console_level()));
    o["log_dir"] = Json(cfg.log_dir);
    o["algo_dir"] = Json(cfg.algo_dir);
    o["state_dir"] = Json(cfg.state_dir);
    o["state_autosave"] = Json(cfg.state_autosave);
    return o;
}

/// Serialize the current runtime state of the five writable settings —
/// exactly what config.json persists (lang is LanguageManager state, not an
/// AppConfig field).
Json runtime_settings_json() {
    Json o = Json::object();
    o["lang"] = Json(std::string(LanguageManager::instance().active().name()));
    o["log_level"] = Json(static_cast<int64_t>(Logger::instance().get_level()));
    o["log_retention"] = Json(static_cast<int64_t>(Logger::instance().get_retention()));
    o["log_console"] = Json(Logger::instance().console_enabled());
    o["log_console_level"] = Json(static_cast<int64_t>(Logger::instance().console_level()));
    return o;
}

void apply_lang(const std::string& code) {
    auto available = LanguageManager::instance().available();
    bool known = false;
    for (const auto& a : available) known = known || a == code;
    if (!known)
        throw WebHttpError(400, "INVALID_FIELD", "unknown language: " + code);
    LanguageManager::instance().select(code);
}

/// Bound-check before casting: out-of-range 0..3 would overflow LogLevel and
/// corrupt the Logger singleton.  Read as int64_t so oversized integers (e.g.
/// 2^32+2) are rejected instead of wrapping down into the valid range.
int32_t checked_log_level(const Json& v, const char* field) {
    int64_t lv = v.as<int64_t>();
    if (lv < 0 || lv > 3)
        throw WebHttpError(400, "INVALID_FIELD", std::string(field) + " must be 0..3");
    return static_cast<int32_t>(lv);
}

} // namespace

Response SettingsController::get() {
    // Same gate as patch(): reads LanguageManager/Logger state that the solve
    // worker (tr/tr_fmt) and concurrent reactor handlers touch.
    std::lock_guard<std::mutex> lock(_gate);
    const uint16_t port = _effective_port ? _effective_port->load() : 0;
    return Response::json(200, "OK", build_settings_json(port).to_string());
}

Response SettingsController::patch(const HttpRequest&, const PathParams&, const Json& body) {
    // Serialize against the solve worker and concurrent reactor handlers:
    // select() mutates LanguageManager::_active; set_level etc. mutate the
    // Logger singleton.
    std::lock_guard<std::mutex> lock(_gate);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "settings body must be a JSON object");

    // as<T>() accessors throw JsonException on type mismatch (e.g.
    // {"log_level":"x"}) — surface that as a 400 INVALID_FIELD, not a raw
    // exception that Router::dispatch would otherwise turn into a 500.
    try {
        if (body.has("lang"))
            apply_lang(body["lang"].as<std::string>());
        if (body.has("log_level"))
            Logger::instance().set_level(
                static_cast<LogLevel>(checked_log_level(body["log_level"], "log_level")));
        if (body.has("log_console"))
            Logger::instance().set_console_enabled(body["log_console"].as<bool>());
        if (body.has("log_console_level"))
            Logger::instance().set_console_level(
                static_cast<LogLevel>(checked_log_level(body["log_console_level"], "log_console_level")));
        // Bound-check before casting: negative would wrap into size_t and
        // turn rotation into "delete everything".  Read as int64_t so
        // oversized integers (e.g. 2^63+1) are rejected instead of wrapping.
        if (body.has("log_retention")) {
            const int64_t rv = body["log_retention"].as<int64_t>();
            if (rv < 0)
                throw WebHttpError(400, "INVALID_FIELD", "log_retention must be >= 0");
            Logger::instance().set_retention(static_cast<size_t>(rv));
        }
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "invalid settings field type");
    }

    // Persist the current runtime settings to <cwd>/config.json so they
    // survive a restart.  Best-effort: the in-memory state above is already
    // applied — a failed write only LOG_WARNs (inside save_config_file) and
    // the PATCH still succeeds.
    AppConfig::save_config_file(runtime_settings_json());

    return Response::json(200, "OK", build_settings_json(_effective_port ? _effective_port->load() : 0).to_string());
}

} // namespace web
