#include "AppConfig.h"
#include "domain/interface/cli/CLIApp.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) try {
    // ── Configuration ──
    auto& app_cfg = AppConfig::get();  // global singleton — consumers read it directly

    // ── i18n setup ──
    register_builtin_translations(LanguageManager::instance());
    // On-demand language file directory (next to executable → langs/<code>.json)
    try {
        LanguageManager::instance().set_langs_dir(
            std::filesystem::path(argv[0]).parent_path() / "langs"
        );
    } catch (...) {}
    CLIApp::apply_lang(argc, argv);

    // ── Logger setup ──
    setup_logger(app_cfg.logger_config());

    // ── Detect target app and route ──
    auto target = CLIApp::detect_target(argc, argv);

    if (target == "cli") {
        // CLIApp owns its output flush: ~CLIApp() (the temporary is destroyed
        // right after run() returns) flushes std::cout and drains the async
        // Logger queue while this process is still alive.  The exit-time
        // implicit flush was intermittently LOSING buffered output (observed
        // on --list-algorithms: the list printed only ~2/3 of runs) because
        // static-teardown ordering is unreliable in the EXE + SHARED
        // besq-common-log layout — see CLIApp::flush_output().
        return CLIApp().run(argc, argv);
    }

    std::cerr << "Unknown API target: " << target << "\n";
    return 1;

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
