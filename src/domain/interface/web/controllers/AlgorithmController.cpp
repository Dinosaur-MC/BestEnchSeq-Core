#include "AlgorithmController.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/components/http/Router.h"
#include "common/io/json.h"
#include <string>

namespace web {

namespace {
const char* origin_name(AlgorithmOrigin o) {
    return o == AlgorithmOrigin::plugin ? "plugin" : "builtin";
}
} // namespace

Response AlgorithmController::list(const HttpRequest&) {
    // Serialize against the solve worker and concurrent reactor handlers:
    // reads the AlgorithmLoader registry.
    std::lock_guard<std::mutex> lock(_gate);
    Json arr = Json::array();
    for (const auto& name : _ctx.list_algorithms())
        arr.push_back(Json(name));
    return Response::json(200, "OK", arr.to_string());
}

Response AlgorithmController::detail(const HttpRequest&, const PathParams& pp) {
    // Same gate as list(): algorithm_detail() reads the loader registry.
    std::lock_guard<std::mutex> lock(_gate);
    const std::string name = pp.get("name");
    AlgorithmDetail d;
    try {
        d = _ctx.algorithm_detail(name);
    } catch (const std::exception&) {
        // algorithm_detail throws std::runtime_error on an unknown name — map
        // it to the API's 404 envelope, not a Router 500.
        throw WebHttpError(404, "ALGORITHM_NOT_FOUND", "algorithm not found: " + name);
    }
    Json o = Json::object();
    o["name"] = Json(d.name);
    o["version"] = Json(d.version);
    o["origin"] = Json(origin_name(d.origin));
    o["plugin_path"] = Json(d.plugin_path);
    o["is_resumable"] = Json(d.is_resumable);
    o["supported_mode"] = Json(d.supported_mode);
    o["has_audit"] = Json(d.has_audit);
    return Response::json(200, "OK", o.to_string());
}

Response AlgorithmController::load(const HttpRequest&, const PathParams&, const Json& body) {
    // Same gate as list(): load_algorithms() mutates the loader registry.
    std::lock_guard<std::mutex> lock(_gate);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "load body must be a JSON object");
    std::string dir;
    try {
        dir = body["dir"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "load body requires a 'dir' string");
    }
    if (dir.empty())
        throw WebHttpError(400, "INVALID_FIELD", "dir must not be empty");
    auto loaded = _ctx.load_algorithms(dir);
    Json o = Json::object();
    o["loaded"] = Json(static_cast<int64_t>(loaded));
    return Response::json(200, "OK", o.to_string());
}

Response AlgorithmController::unload(const HttpRequest&, const PathParams&, const Json& body) {
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "unload body must be a JSON object");
    std::string name;
    try {
        name = body["name"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "unload body requires a 'name' string");
    }
    // A running solve may be holding the plugin's executor instance — refuse
    // to unload while one is active (single-slot invariant).  Checked BEFORE
    // the gate: the worker no longer holds the gate for the whole solve (P0
    // snapshot decoupling — it only takes it briefly for snapshot build and
    // format), so a gate-first check would block behind those µs/ms windows
    // only to reach a decision that's already made. has_active() reads task
    // state (mutex-guarded) and the check is advisory — the mutation below is
    // still serialized on the gate, and a solve starting between check and
    // mutation fails cleanly (its create_executor happens after ours, and the
    // loader rejects the unloaded algorithm).
    if (_svc.has_active())
        throw WebHttpError(409, "TASK_ACTIVE", "cannot unload algorithm while a solve is running");
    // Same gate as list(): unload_algorithm() mutates the loader registry.
    std::lock_guard<std::mutex> lock(_gate);
    // False = builtin (trusted kernel) or unknown — never unloadable.
    if (!_ctx.unload_algorithm(name))
        throw WebHttpError(400, "UNLOAD_REJECTED", "unload rejected: " + name);
    Json o = Json::object();
    o["ok"] = Json(true);
    return Response::json(200, "OK", o.to_string());
}

} // namespace web
