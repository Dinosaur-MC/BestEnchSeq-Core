#pragma once
#include "domain/interface/components/http/HttpController.h"
#include <mutex>

class BesqContext;

namespace web {

/// GET/PATCH /api/settings. `lang`, `log_level`, `log_console`,
/// `log_console_level` are writable at runtime; `gui_host`/`gui_port` are
/// startup-only (the server is already bound).
///
/// Every handler holds _gate FIRST — the same web-layer _ctx_gate that
/// WebSolveService holds for its whole _ctx window. patch() mutates
/// LanguageManager::_active (a global singleton the solve worker reads via
/// tr/tr_fmt) and the Logger singleton; the reactor threads run handlers
/// concurrently, so unlocked access would be a data race.
class SettingsController : public HttpController<SettingsController> {
public:
    using Self = SettingsController;

    explicit SettingsController(BesqContext& ctx, std::mutex& gate) : _ctx(ctx), _gate(gate) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/api/settings", get),
            BESQ_ROUTE(Patch, "/api/settings", patch),
        };
    }

    Response get();
    Response patch(const HttpRequest&, const PathParams&, const Json&);

private:
    BesqContext& _ctx;
    std::mutex& _gate;
};

} // namespace web
