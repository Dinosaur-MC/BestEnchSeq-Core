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
        const int rc = CLIApp().run(argc, argv);
        // Explicitly flush stdio/iostream BEFORE the process exits.  The CLI
        // shares stdout with the async Logger's thread, and the exit-time
        // flush was intermittently LOSING buffered output (observed on
        // --list-algorithms: the list printed only ~2/3 of runs).  Flushing
        // here, while the main thread is alive, makes CLI output reliable.
        // The async Logger's queue is drained the same way: the process can
        // exit before its worker thread writes queued WARN/ERROR lines (e.g.
        // the plugin audit's "[Audit] REFUSED" — a critical signal that must
        // not be lost), so wait for the queue to drain while alive.
        besq::log::flush();
        std::cout.flush();
        std::cerr.flush();
        return rc;
    }

    std::cerr << "Unknown API target: " << target << "\n";
    return 1;

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
