#include "ApiHealth.h"
#include "common/io/json.h"
#include "BuildConfig.h"
#include <chrono>

namespace {
inline int64_t uptime_ms() {
    static const auto started = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started)
        .count();
}
} // namespace

std::string ApiHealth::handle() {
    Json o = Json::object();
    o["status"] = Json("ok");
    o["version"] = Json(BESQ_VERSION);
    o["uptime_ms"] = Json(uptime_ms());
    return o.to_string();
}
