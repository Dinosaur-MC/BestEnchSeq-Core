#include "SettingsController.h"
#include "AppConfig.h"
#include "common/i18n/Language.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "domain/interface/components/http/Router.h"

namespace web {

namespace {

/// Current settings snapshot — mirrors the old ApiSettings::handle_get
/// (field names/shape unchanged; machine format, not localized).
Json build_settings_json() {
    const auto& cfg = AppConfig::get();
    Json o = Json::object();
    o["lang"] = Json(std::string(LanguageManager::instance().active().name()));
    o["gui_host"] = Json(cfg.gui_host);
    o["gui_port"] = Json(static_cast<int64_t>(cfg.gui_port));
    o["gui_open_browser"] = Json(cfg.gui_open_browser);
    o["log_level"] = Json(static_cast<int64_t>(Logger::instance().get_level()));
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
    return Response::json(200, "OK", build_settings_json().to_string());
}

Response SettingsController::patch(const HttpRequest&, const PathParams&, const Json& body) {
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
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "invalid settings field type");
    }

    return Response::json(200, "OK", build_settings_json().to_string());
}

} // namespace web
