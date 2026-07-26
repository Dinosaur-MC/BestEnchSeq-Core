#include "AppConfig.h"
#include "common/log/log.hpp"
#include "common/utils/StringUtils.hpp"
#include "domain/interface/cli/CLIApp.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/LocaleDetector.h"
#include "common/i18n/Language.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) try {
    // ── Configuration ──
    auto app_cfg = AppConfig::load();

    // ── i18n setup ──
    auto& lang_mgr = LanguageManager::instance();
    register_builtin_translations(lang_mgr);

    {
        const char* env_lang = std::getenv("BESQ_LANG");
        std::string lang_code = env_lang ? env_lang : detect_system_locale();
        lang_mgr.select(lang_mgr.resolve_locale(lang_code));
    }

    // Light pre-parse for --lang (before full CLI parse)
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string_view(argv[i]) == "--lang") {
            lang_mgr.select(lang_mgr.resolve_locale(argv[i + 1]));
            break;
        }
    }

    // ── Logger setup ──
    Logger::instance().set_level(
        app_cfg.log_level >= 3 ? LogLevel::Error
      : app_cfg.log_level >= 2 ? LogLevel::Warn
      : app_cfg.log_level >= 1 ? LogLevel::Info
      :                          LogLevel::Debug);
    Logger::instance().set_retention(app_cfg.log_retention);

    // ── Detect target app and route ──
    auto target = CLIApp::detect_target(argc, argv);

    if (target == "cli")
        return CLIApp::run(argc, argv);

    std::cerr << "Unknown API target: " << target << "\n";
    return 1;

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
