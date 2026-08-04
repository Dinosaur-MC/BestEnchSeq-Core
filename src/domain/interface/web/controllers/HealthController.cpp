#include "HealthController.h"
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

Response HealthController::health() {
    Json o = Json::object();
    o["status"] = Json("ok");
    o["uptime_ms"] = Json(uptime_ms());
    return Response::json(200, "OK", o.to_string());
}

} // namespace web
