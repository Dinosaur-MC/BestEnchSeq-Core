#pragma once
#include "domain/interface/components/http/HttpController.h"
#include <mutex>

namespace web {

/// GET/PATCH /api/settings. `lang`, `log_level`, `log_console`,
/// `log_console_level` are writable at runtime; `gui_host`/`gui_port`/
/// `gui_workers`/`memory_mb`/`sandbox_enabled` are read-only (set at
/// startup — the server is already bound).  Every successful PATCH persists
/// the four writable fields to <cwd>/config.json (best-effort; the GUI
/// reloads them at startup — see AppConfig).
///
/// Every handler holds _gate FIRST — the same web-layer _ctx_gate that
/// WebSolveService holds for its whole _ctx window. patch() mutates
/// LanguageManager::_active (a global singleton the solve worker reads via
/// tr/tr_fmt) and the Logger singleton; the reactor threads run handlers
/// concurrently, so unlocked access would be a data race.
class SettingsController : public HttpController<SettingsController> {
public:
    using Self = SettingsController;

    explicit SettingsController(std::mutex& gate) : _gate(gate) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/api/settings", get),
            BESQ_ROUTE(Patch, "/api/settings", patch),
        };
    }

    Response get();
    Response patch(const HttpRequest&, const PathParams&, const Json&);

private:
    std::mutex& _gate;
};

} // namespace web
