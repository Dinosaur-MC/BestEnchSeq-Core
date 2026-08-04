#include "CalculatorController.h"
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/SseHub.h"
#include "domain/interface/web/WebSchema.h"
#include "domain/interface/components/http/Router.h"
#include "ds/Error.h"
#include "common/io/json.h"
#include <string>

namespace web {

namespace {
const char* state_name(webhttp::TaskState s) {
    switch (s) {
        case webhttp::TaskState::Running:   return "running";
        case webhttp::TaskState::Completed: return "completed";
        case webhttp::TaskState::Failed:    return "failed";
        case webhttp::TaskState::Cancelled: return "cancelled";
    }
    return "unknown";
}

/// `text/event-stream` stream response. is_stream=true means the transport
/// switches to chunked streaming and ignores `body` (frame writing is wired in
/// Task 18).
HttpResponse sse_response() {
    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = "text/event-stream";
    r.is_stream = true;
    return r;
}

/// The service layer throws the legacy 2-arg `webhttp::WebHttpError`
/// (WebSolveService.h), while the Router only catches the 3-arg
/// `web::WebHttpError` (Router.h). Without a bridge a service conflict/404
/// would surface as a generic 500. Map the status to a stable API code and
/// rethrow in the Router's dialect.
[[noreturn]] void rethrow_webhttp_error(const webhttp::WebHttpError& e) {
    const char* code = e.status == 404 ? "TASK_NOT_FOUND"
                     : e.status == 409 ? "TASK_ACTIVE"
                     : "REQUEST_FAILED";
    throw WebHttpError(e.status, code, e.what());
}

/// Run a WebSolveService call, converting its legacy error type to the
/// Router's WebHttpError on the way out.
template <typename F>
auto bridge_service(F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const webhttp::WebHttpError& e) {
        rethrow_webhttp_error(e);
    }
}
} // namespace

Response CalculatorController::submit(const HttpRequest&, const PathParams&, const Json& body) {
    WebTaskDto dto;
    try {
        WebTaskJson::parse_or_throw(body, dto);
    } catch (const ds::ValidationError&) {
        // Schema violation (missing/wrong-typed field) — 400 INVALID_TASK.
        // (Router only maps JsonException→400 INVALID_BODY, so ValidationError
        // must be converted here or it would surface as a 500.)
        throw WebHttpError(400, "INVALID_TASK", "invalid task body");
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_TASK", "invalid task JSON");
    }
    // start() throws webhttp::WebHttpError(409) on active-slot conflict —
    // bridged to the Router's WebHttpError so it surfaces as 409, not a 500.
    std::string id = bridge_service([&] { return _svc.start(dto); });
    Json o = Json::object();
    o["task_id"] = Json(id);
    return Response::accepted("/api/tasks/" + id, o.to_string());
}

Response CalculatorController::status(const HttpRequest&, const PathParams& pp) {
    auto st = bridge_service([&] { return _svc.status(pp.get("id")); });  // 404 when unknown
    Json o = Json::object();
    o["state"] = Json(state_name(st.state));
    o["progress"] = Json(st.progress);
    if (st.state == webhttp::TaskState::Completed)
        o["result"] = Json::parse(st.result);
    else if (st.state == webhttp::TaskState::Failed)
        o["error"] = Json(st.error);
    return Response::json(200, "OK", o.to_string());
}

Response CalculatorController::cancel(const HttpRequest&, const PathParams& pp) {
    const std::string id = pp.get("id");
    // status() distinguishes "unknown" (404) from "already finished" — the
    // service's cancel() bool alone cannot tell them apart. DELETE on a
    // finished task is a successful no-op.
    (void)bridge_service([&] { return _svc.status(id); });   // 404 when unknown
    _svc.cancel(id);
    Json o = Json::object();
    o["ok"] = Json(true);
    return Response::json(200, "OK", o.to_string());
}

Response CalculatorController::events(const HttpRequest&, const PathParams& pp) {
    const std::string id = pp.get("id");
    // Validate the task exists before opening a stream — unknown → 404.
    (void)bridge_service([&] { return _svc.status(id); });   // 404 when unknown
    // Register an SSE subscription. The callback is a no-op placeholder: frame
    // delivery to the connection is the transport's job (Task 18). The sub id
    // is retained so that task can unsubscribe on disconnect.
    auto sub = _hub.subscribe(id, [](const std::string&, std::string) {});
    _streams[id] = sub;
    return sse_response();
}

} // namespace web
