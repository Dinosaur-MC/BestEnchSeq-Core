#include "StatusController.h"
#include "domain/interface/BesqContext.h"
#include "domain/algorithm/types/AlgorithmState.h"
#include "common/io/json.h"
#include <chrono>

namespace {
inline int64_t uptime_ms() {
    static const auto started = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started)
        .count();
}
} // namespace

namespace web {

Response StatusController::status() {
    // Serialize against the solve worker and concurrent reactor handlers:
    // reads ProfileManager active/_registry + AlgorithmLoader registry.
    std::lock_guard<std::mutex> lock(_gate);
    Json o = Json::object();
    o["active_profile"] = Json(_ctx.active_profile());
    o["profile_count"] = Json(static_cast<int64_t>(_ctx.list_profiles().size()));
    o["algorithm_count"] = Json(static_cast<int64_t>(_ctx.list_algorithms().size()));
    auto prog = _ctx.solve_progress();
    o["has_active_solve"] = Json(prog.state != algorithm::AlgorithmState::Idle);
    o["uptime_ms"] = Json(uptime_ms());
    return Response::json(200, "OK", o.to_string());
}

} // namespace web
