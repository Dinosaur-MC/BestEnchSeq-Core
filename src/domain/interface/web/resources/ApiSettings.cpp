#include "ApiSettings.h"
#include "AppConfig.h"
#include "common/i18n/Language.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebHttpError.h"

std::string ApiSettings::handle_get(const BesqContext& ctx) {
    (void)ctx;
    const auto& cfg = AppConfig::get();
    Json o = Json::object();
    o["lang"] = Json(std::string(LanguageManager::instance().active().name()));
    o["gui_host"] = Json(cfg.gui_host);
    o["gui_port"] = Json(static_cast<int64_t>(cfg.gui_port));
    o["gui_open_browser"] = Json(cfg.gui_open_browser);
    o["log_level"] = Json(static_cast<int64_t>(Logger::instance().get_level()));
    o["log_console"] = Json(Logger::instance().console_enabled());
    o["log_console_level"] = Json(static_cast<int64_t>(Logger::instance().console_level()));
    return o.to_string();
}

std::string ApiSettings::handle_put(BesqContext& ctx, const Json& body) {
    (void)ctx;
    if (body.type() != JsonType::Object)
        throw webhttp::WebHttpError(400, "settings body must be a JSON object");

    auto apply_lang = [&](const std::string& code) {
        if (code.empty()) return;
        auto available = LanguageManager::instance().available();
        bool known = false;
        for (const auto& a : available) known = known || a == code;
        if (!known)
            throw webhttp::WebHttpError(400, "unknown language: " + code);
        LanguageManager::instance().select(code);
    };

    if (body.has("lang"))
        apply_lang(body["lang"].as<std::string>());
    if (body.has("log_level"))
        Logger::instance().set_level(static_cast<LogLevel>(body["log_level"].as<int32_t>()));
    if (body.has("log_console"))
        Logger::instance().set_console_enabled(body["log_console"].as<bool>());
    if (body.has("log_console_level"))
        Logger::instance().set_console_level(
            static_cast<LogLevel>(body["log_console_level"].as<int32_t>()));

    return handle_get(ctx);
}
