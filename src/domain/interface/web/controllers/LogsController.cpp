#include "LogsController.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/SseHub.h"
#include "domain/interface/components/http/Router.h"
#include "common/log/log.hpp"
#include "common/log/LogRingBuffer.h"
#include "common/io/json.h"
#include <cerrno>
#include <cstdlib>
#include <string>

namespace web {

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

/// Strict int64 parse: empty / trailing junk / overflow → false.
bool parse_i64(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE || end == s.c_str() || *end != '\0') return false;
    out = static_cast<int64_t>(v);
    return true;
}

HttpResponse sse_response() {
    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = "text/event-stream";
    r.is_stream = true;
    return r;
}
} // namespace

Response LogsController::tail(const HttpRequest& req) {
    int64_t since = 0;
    if (req.query.has("since")) {
        if (!parse_i64(req.query.get("since"), since) || since < 0)
            throw WebHttpError(400, "INVALID_FIELD", "since must be a non-negative integer");
    }
    bool has_limit = req.query.has("limit");
    int64_t limit = 0;
    if (has_limit) {
        if (!parse_i64(req.query.get("limit"), limit) || limit < 0)
            throw WebHttpError(400, "INVALID_FIELD", "limit must be a non-negative integer");
    }
    // Default 200 (matches the old ApiLogs tail); an explicit 0 means "return
    // nothing" — but snapshot(tail=0) would erase everything, so guard it.
    size_t tail_n = (!has_limit || limit == 0) ? 200 : static_cast<size_t>(limit);

    auto ring = Logger::instance().ring_buffer();
    Json arr = Json::array();
    int64_t next = since;
    if (ring) {
        for (const auto& e : ring->snapshot(LogLevel::Debug, tail_n)) {
            // LogRingBuffer has no per-entry sequence number — the incremental
            // cursor is the record's millisecond timestamp (monotonic in
            // practice). `since` filters to strictly newer records.
            const int64_t seq = e.timestamp_ms;
            if (seq <= since) continue;
            Json o = Json::object();
            o["seq"] = Json(seq);
            o["level"] = Json(level_name(e.level));
            o["timestamp_ms"] = Json(e.timestamp_ms);
            o["message"] = Json(e.message);
            arr.push_back(o);
            if (seq > next) next = seq;
        }
    }
    Json root = Json::object();
    root["logs"] = arr;
    root["next"] = Json(next);
    return Response::json(200, "OK", root.to_string());
}

Response LogsController::events(const HttpRequest&) {
    // Register an SSE subscription under the synthetic "logs" task key. The
    // transport wiring (Task 18) pushes new ring records to the hub; this
    // handler only validates the route and returns a stream response.
    _hub.subscribe("logs", [](const std::string&, std::string) {});
    return sse_response();
}

} // namespace web
