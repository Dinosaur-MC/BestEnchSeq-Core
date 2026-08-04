#pragma once
#include <string>

namespace webhttp { class WebSolveService; }

/// /api/calculator — async solve lifecycle.
///
///   POST   /api/calculator   {WebTask JSON}        → {task_id}
///   GET    /api/calculator/{id}  → {state, progress} or {state:"completed", result}
///   DELETE /api/calculator/{id}  → {ok:true}
struct ApiCalculator {
    static std::string handle_post(webhttp::WebSolveService& svc, const std::string& body);
    static std::string handle_get(webhttp::WebSolveService& svc, const std::string& id);
    static std::string handle_del(webhttp::WebSolveService& svc, const std::string& id);
};
