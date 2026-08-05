#pragma once
#include "domain/interface/components/http/HttpController.h"
#include <mutex>

class BesqContext;

namespace web {

/// GET /api/status — session snapshot (active profile, profile count,
/// algorithm count, active solve, uptime).
///
/// Every handler holds _gate FIRST — the same web-layer _ctx_gate that
/// WebSolveService holds for its whole _ctx window and ProfilesController
/// holds around every profile call. status() reads ProfileManager's active
/// profile and AlgorithmLoader's registry; the reactor threads run handlers
/// concurrently and the solve worker mutates the same state, so unlocked
/// access would be a data race.
class StatusController : public HttpController<StatusController> {
public:
    using Self = StatusController;

    explicit StatusController(BesqContext& ctx, std::mutex& gate) : _ctx(ctx), _gate(gate) {}

    static constexpr auto route_defs() {
        return std::array{ BESQ_ROUTE(Get, "/api/status", status) };
    }

    Response status();

private:
    BesqContext& _ctx;
    std::mutex& _gate;
};

} // namespace web
