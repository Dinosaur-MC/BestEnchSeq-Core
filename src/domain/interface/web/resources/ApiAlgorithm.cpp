#include "ApiAlgorithm.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebHttpError.h"
#include "common/io/json.h"
#include <vector>

std::string ApiAlgorithm::handle_list(const BesqContext& ctx) {
    Json arr = Json::array();
    for (const auto& name : ctx.list_algorithms()) {
        Json o = Json::object();
        o["name"] = Json(name);
        arr.push_back(o); // Json::Array is std::vector<Json> — use push_back
    }
    Json root = Json::object();
    root["algorithms"] = arr;
    return root.to_string();
}

std::string ApiAlgorithm::handle_get(const BesqContext& ctx, const std::string& name) {
    auto all = ctx.list_algorithms();
    bool found = false;
    for (const auto& n : all) found = found || (n == name);
    if (!found)
        throw webhttp::WebHttpError(404, "unknown algorithm: " + name);
    Json o = Json::object();
    o["name"] = Json(name);
    // Full per-strategy metadata (version/mode) requires IExecutor; the
    // facade exposes only names. Keep mode empty for v1.
    o["mode"] = Json("");
    return o.to_string();
}

std::string ApiAlgorithm::handle_load(BesqContext& ctx, const std::string& dir) {
    auto loaded = ctx.load_algorithms(dir);
    Json o = Json::object();
    o["loaded"] = Json(static_cast<int64_t>(loaded));
    return o.to_string();
}
