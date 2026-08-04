#include "ApiLogs.h"
#include "common/log/LogRingBuffer.h"
#include "common/io/json.h"

namespace {
const char* level_name(LogLevel lv) {
    switch (lv) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
    }
    return "unknown";
}
} // namespace

std::string ApiLogs::handle(const LogRingBuffer& ring,
                            LogLevel min_level, size_t tail) {
    auto entries = ring.snapshot(min_level, tail);
    Json arr = Json::array();
    for (const auto& e : entries) {
        Json o = Json::object();
        o["level"] = Json(level_name(e.level));
        o["timestamp_ms"] = Json(e.timestamp_ms);
        o["message"] = Json(e.message);
        arr.push_back(o);
    }
    Json root = Json::object();
    root["logs"] = arr;
    return root.to_string();
}
