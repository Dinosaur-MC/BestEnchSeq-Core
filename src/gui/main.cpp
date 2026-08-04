#include "AppConfig.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "domain/interface/BesqContext.h"
#include <iostream>
#include <string>

/// besq-gui — localhost Web GUI host (WebView2 native window, or default
/// browser in dev mode via BESQ_GUI_OPEN_BROWSER / --browser).
int main(int argc, char* argv[]) try {
    auto& cfg = AppConfig::get();

    register_builtin_translations(LanguageManager::instance());
    setup_logger(cfg.logger_config());

    // WebView2 host and HTTP server land in later milestones (M1.7/M4.3);
    // for now print the configured bind address as a placeholder.
    bool open_browser = cfg.gui_open_browser;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--browser") open_browser = true;

    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    std::cerr << "besq-gui: Web module lands in M1; nothing to serve yet.\n";
    std::cerr << "open_browser=" << (open_browser ? "true" : "false") << "\n";
    std::cerr << "host=" << cfg.gui_host << " port=" << cfg.gui_port << "\n";
    return 0;

} catch (const std::exception& e) {
    std::cerr << "besq-gui: " << e.what() << std::endl;
    return 1;
}
