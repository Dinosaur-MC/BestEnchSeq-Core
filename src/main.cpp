#include "AppConfig.h"
#include "domain/interface/cli/CLIApp.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"

#include <iostream>

int main(int argc, char* argv[]) try {
    // ── Configuration ──
    auto app_cfg = AppConfig::load();

    // ── i18n setup ──
    register_builtin_translations(LanguageManager::instance());
    CLIApp::apply_lang(argc, argv);

    // ── Logger setup ──
    setup_logger(app_cfg.log_level, app_cfg.log_retention);

    // ── Detect target app and route ──
    auto target = CLIApp::detect_target(argc, argv);

    if (target == "cli")
        return CLIApp().run(argc, argv);

    std::cerr << "Unknown API target: " << target << "\n";
    return 1;

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
