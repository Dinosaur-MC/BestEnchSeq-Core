#pragma once
#include "domain/interface/components/http/HttpController.h"

class BesqContext;

namespace web {

/// GET/PATCH /api/settings. `lang`, `log_level`, `log_console`,
/// `log_console_level` are writable at runtime; `gui_host`/`gui_port` are
/// startup-only (the server is already bound).
class SettingsController : public HttpController<SettingsController> {
public:
    using Self = SettingsController;

    explicit SettingsController(BesqContext& ctx) { (void)ctx; }

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/api/settings", get),
            BESQ_ROUTE(Patch, "/api/settings", patch),
        };
    }

    Response get();
    Response patch(const HttpRequest&, const PathParams&, const Json&);
};

} // namespace web
