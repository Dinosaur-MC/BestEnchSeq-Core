#include "ApiStatus.h"
#include "domain/interface/BesqContext.h"
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

std::string ApiStatus::handle(const BesqContext& ctx) {
    Json o = Json::object();
    o["active_profile"] = Json(ctx.active_profile());
    o["profile_count"] = Json(static_cast<int64_t>(ctx.list_profiles().size()));
    o["algorithm_count"] = Json(static_cast<int64_t>(ctx.list_algorithms().size()));
    o["has_active_solve"] = Json(false); // ← replaced by ctx.solve_progress() in M2.1
    o["uptime_ms"] = Json(uptime_ms());
    return o.to_string();
}
