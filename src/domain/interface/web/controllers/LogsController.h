#pragma once
#include "domain/interface/components/http/HttpController.h"

class BesqContext;

namespace web {
class SseHub;

/// GET /api/logs (+ /api/logs/events) — recent log lines from the Logger's
/// ring buffer. Replaces the old ApiLogs resource (web-http-modernize §6.2)
/// with an incremental contract:
///   GET /api/logs?since=<seq>&limit=<n> → {"logs":[...], "next":<next-seq>}
/// where `next` is the cursor to pass back as `?since=`. Each record carries
/// `seq` (the monotonic cursor), `level`, `timestamp_ms`, `message`.
///
/// /api/logs/events is an SSE stream response; the transport task (Task 18)
/// pushes new records onto the hub under the synthetic "logs" key.
class LogsController : public HttpController<LogsController> {
public:
    using Self = LogsController;

    LogsController(BesqContext& ctx, SseHub& hub) : _hub(hub) { (void)ctx; }

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/api/logs",        tail),
            BESQ_ROUTE(Get, "/api/logs/events", events),
        };
    }

    Response tail(const HttpRequest&);
    Response events(const HttpRequest&);

private:
    SseHub& _hub;
};

} // namespace web
