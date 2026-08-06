#pragma once
#include "domain/interface/components/http/HttpController.h"
#include "domain/interface/web/SseHub.h"
#include <string>
#include <unordered_map>

namespace web {
class WebSolveService;

/// POST/GET/DELETE /api/tasks — async solve submission + polled status +
/// cancel + SSE subscription. Replaces the old ApiCalculator resource
/// (web-http-modernize §6.2). `/api/tasks/{id}/events` returns a stream
/// response and registers an SseHub subscription; frame delivery to the wire is
/// wired up in the transport task (Task 18).
class CalculatorController : public HttpController<CalculatorController> {
public:
    using Self = CalculatorController;

    CalculatorController(WebSolveService& svc, SseHub& hub)
        : _svc(svc), _hub(hub) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Post,   "/api/tasks",             submit),
            BESQ_ROUTE(Get,    "/api/tasks/{id}",        status),
            BESQ_ROUTE(Delete, "/api/tasks/{id}",        cancel),
            BESQ_ROUTE(Post,   "/api/tasks/{id}/pause",  pause),
            BESQ_ROUTE(Post,   "/api/tasks/{id}/resume", resume),
            BESQ_ROUTE(Get,    "/api/tasks/{id}/events", events),
        };
    }

    Response submit(const HttpRequest&, const PathParams&, const Json&);
    Response status(const HttpRequest&, const PathParams&);
    Response cancel(const HttpRequest&, const PathParams&);
    Response pause(const HttpRequest&, const PathParams&);
    Response resume(const HttpRequest&, const PathParams&);
    Response events(const HttpRequest&, const PathParams&);

private:
    WebSolveService& _svc;
    SseHub& _hub;
    /// active SSE subscription id → task id. Keyed by SubId so concurrent
    /// connections on the same task (SPA tabs/reconnects) each keep their own
    /// cleanup record; a task-id-keyed map let a second connect overwrite the
    /// first subscriber's entry, leaking the stale record when it closed.
    std::unordered_map<SseHub::SubId, std::string> _streams;
};

} // namespace web
