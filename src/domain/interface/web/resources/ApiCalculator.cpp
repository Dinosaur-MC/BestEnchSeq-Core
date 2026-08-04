#include "ApiCalculator.h"
#include "domain/interface/web/WebSchema.h"
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/WebHttpError.h"
#include "common/io/json.h"
#include "ds/ds.h"

namespace {

const char* state_name(webhttp::TaskState s) {
    switch (s) {
        case webhttp::TaskState::Running:   return "running";
        case webhttp::TaskState::Completed: return "completed";
        case webhttp::TaskState::Failed:    return "failed";
        case webhttp::TaskState::Cancelled: return "cancelled";
    }
    return "running";
}

} // namespace

std::string ApiCalculator::handle_post(webhttp::WebSolveService& svc, const std::string& body) {
    auto json = Json::parse(body);
    WebTaskDto dto;
    WebTaskJson::parse_or_throw(json, dto);
    auto id = svc.start(dto);   // throws WebHttpError(409) on active conflict
    Json o = Json::object();
    o["task_id"] = Json(id);
    return o.to_string();
}

std::string ApiCalculator::handle_get(webhttp::WebSolveService& svc, const std::string& id) {
    auto st = svc.status(id);   // throws WebHttpError(404) when unknown
    Json o = Json::object();
    o["state"] = Json(state_name(st.state));
    o["progress"] = Json(st.progress);
    if (st.state == webhttp::TaskState::Completed) {
        o["result"] = Json::parse(st.result);
    } else if (st.state == webhttp::TaskState::Failed) {
        o["error"] = Json(st.error);
    }
    return o.to_string();
}

std::string ApiCalculator::handle_del(webhttp::WebSolveService& svc, const std::string& id) {
    svc.cancel(id);  // no-op when already finished
    Json o = Json::object();
    o["ok"] = Json(true);
    return o.to_string();
}
