#pragma once
#include "domain/interface/components/http/HttpController.h"

class BesqContext;

namespace web {

/// GET /api/status — session snapshot (active profile, profile count,
/// algorithm count, active solve, uptime).
class StatusController : public HttpController<StatusController> {
public:
    using Self = StatusController;

    explicit StatusController(BesqContext& ctx) : _ctx(ctx) {}

    static constexpr auto route_defs() {
        return std::array{ BESQ_ROUTE(Get, "/api/status", status) };
    }

    Response status();

private:
    BesqContext& _ctx;
};

} // namespace web
