#pragma once
#include "domain/interface/components/http/HttpController.h"
#include "domain/interface/web/SseHub.h"
#include <string>
#include <unordered_map>

namespace webhttp {
class WebSolveService;
}

namespace web {

/// POST/GET/DELETE /api/tasks — async solve submission + polled status +
/// cancel + SSE subscription. Replaces the old ApiCalculator resource
/// (web-http-modernize §6.2). `/api/tasks/{id}/events` returns a stream
/// response and registers an SseHub subscription; frame delivery to the wire is
/// wired up in the transport task (Task 18).
class CalculatorController : public HttpController<CalculatorController> {
public:
    using Self = CalculatorController;

    CalculatorController(webhttp::WebSolveService& svc, SseHub& hub)
        : _svc(svc), _hub(hub) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Post,   "/api/tasks",             submit),
            BESQ_ROUTE(Get,    "/api/tasks/{id}",        status),
            BESQ_ROUTE(Delete, "/api/tasks/{id}",        cancel),
            BESQ_ROUTE(Get,    "/api/tasks/{id}/events", events),
        };
    }

    Response submit(const HttpRequest&, const PathParams&, const Json&);
    Response status(const HttpRequest&, const PathParams&);
    Response cancel(const HttpRequest&, const PathParams&);
    Response events(const HttpRequest&, const PathParams&);

private:
    webhttp::WebSolveService& _svc;
    SseHub& _hub;
    /// task id → active SSE subscription id. Retained so the transport task can
    /// unsubscribe on disconnect; a second connect on the same task overwrites
    /// the earlier subscription id (accepted for now, revisited in Task 18).
    std::unordered_map<std::string, SseHub::SubId> _streams;
};

} // namespace web
