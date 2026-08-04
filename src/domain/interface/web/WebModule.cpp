#include "WebModule.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebHttpError.h"
#include "domain/interface/web/resources/ApiHealth.h"
#include "domain/interface/web/resources/ApiSettings.h"
#include "domain/interface/web/resources/ApiAlgorithm.h"
#include "domain/interface/web/resources/ApiLogs.h"
#include "domain/interface/web/resources/ApiStatus.h"
#include "domain/interface/web/resources/ApiCalculator.h"
#include "common/log/log.hpp"          // Logger::instance() for /api/logs
#include "common/log/LogRingBuffer.h"
#include "common/io/json.h"
#include <utility>                     // std::move

namespace webhttp {

namespace {
const char* reason_for(int status) {
    switch (status) {
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
    }
    return "Error";
}
std::string error_json(const std::string& msg) {
    Json o = Json::object();
    o["ok"] = Json(false);
    o["error"] = Json(msg);
    return o.to_string();
}
} // namespace

WebModule::WebModule(BesqContext& ctx) : _ctx(ctx), _solve(ctx, _ctx_gate) {
    _routes = {
        {"GET",  "/health",             [](WebModule& m, const std::vector<std::string>&, const std::string&) { (void)m; return ApiHealth::handle(); }},
        {"GET",  "/api/settings",       [](WebModule& m, const std::vector<std::string>&, const std::string&) { return ApiSettings::handle_get(m._ctx); }},
        {"PUT",  "/api/settings",       [](WebModule& m, const std::vector<std::string>&, const std::string& b) { return ApiSettings::handle_put(m._ctx, Json::parse(b)); }},
        {"GET",  "/api/profile",        [](WebModule& m, const std::vector<std::string>&, const std::string&) {
            std::lock_guard<std::mutex> lock(m._ctx_gate);
            return ApiProfiles::handle_list(m._ctx);
        }},
        {"POST", "/api/profile",        [](WebModule& m, const std::vector<std::string>&, const std::string& b) {
            std::lock_guard<std::mutex> lock(m._ctx_gate);
            return ApiProfiles::handle_action(m._ctx, Json::parse(b));
        }},
        {"GET",  "/api/profile/{id}/{kind}",
                                        [](WebModule& m, const std::vector<std::string>& p, const std::string&) {
            std::lock_guard<std::mutex> lock(m._ctx_gate);
            return ApiProfiles::handle_read(m._ctx, p[0], p[1]);
        }},
        {"POST", "/api/profile/{id}/{kind}",
                                        [](WebModule& m, const std::vector<std::string>& p, const std::string& b) {
            std::lock_guard<std::mutex> lock(m._ctx_gate);
            return ApiProfiles::handle_add(m._ctx, p[0], p[1], Json::parse(b));
        }},
        {"DELETE", "/api/profile/{id}/{kind}/{name}",
                                        [](WebModule& m, const std::vector<std::string>& p, const std::string&) {
            std::lock_guard<std::mutex> lock(m._ctx_gate);
            return ApiProfiles::handle_remove(m._ctx, p[0], p[1], p[2]);
        }},
        {"GET",  "/api/algorithm",      [](WebModule& m, const std::vector<std::string>&, const std::string&) { return ApiAlgorithm::handle_list(m._ctx); }},
        {"GET",  "/api/algorithm/{name}",[](WebModule& m, const std::vector<std::string>& p, const std::string&) { return ApiAlgorithm::handle_get(m._ctx, p[0]); }},
        {"POST", "/api/algorithm/load", [](WebModule& m, const std::vector<std::string>&, const std::string& b) { return ApiAlgorithm::handle_load(m._ctx, Json::parse(b)["dir"].as<std::string>()); }},
        {"POST", "/api/calculator",     [](WebModule& m, const std::vector<std::string>&, const std::string& b) { return ApiCalculator::handle_post(m._solve, b); }},
        {"GET",  "/api/calculator/{id}",[](WebModule& m, const std::vector<std::string>& p, const std::string&) { return ApiCalculator::handle_get(m._solve, p[0]); }},
        {"DELETE", "/api/calculator/{id}",[](WebModule& m, const std::vector<std::string>& p, const std::string&) { return ApiCalculator::handle_del(m._solve, p[0]); }},
        {"GET",  "/api/logs",           [](WebModule& m, const std::vector<std::string>&, const std::string&) {
            (void)m;
            auto ring = Logger::instance().ring_buffer();
            static LogRingBuffer empty(0);
            return ApiLogs::handle(ring ? *ring : empty, LogLevel::Debug, 200);
        }},
        {"GET",  "/api/status",         [](WebModule& m, const std::vector<std::string>&, const std::string&) { return ApiStatus::handle(m._ctx); }},
    };
}

void WebModule::set_static_resources(std::map<std::string, StaticResource> resources) {
    _static = std::move(resources);
}

HttpResponse WebModule::dispatch(const std::string& method, const std::string& path,
                                 const std::string& body) {
    try {
        for (const auto& route : _routes) {
            if (route.method != method) continue;
            std::vector<std::string> params;
            if (match_pattern(route.pattern, path, params)) {
                std::string result = route.handler(*this, params, body);
                return HttpResponse::json(200, "OK", result);
            }
        }

        // Static assets (only for GET).
        if (method == "GET") {
            std::string key = path.empty() || path == "/" ? "/index.html" : path;
            auto it = _static.find(key);
            if (it != _static.end()) {
                HttpResponse resp;
                resp.status = 200;
                resp.content_type = it->second.content_type;
                resp.body = it->second.content;
                return resp;
            }
        }
        return HttpResponse::json(404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}");
    } catch (const WebHttpError& e) {
        return HttpResponse::json(e.status, reason_for(e.status), error_json(e.what()));
    } catch (const JsonException&) {
        return HttpResponse::json(400, "Bad Request", error_json("invalid request body"));
    } catch (const std::exception& e) {
        return HttpResponse::json(500, "Internal Server Error", error_json(e.what()));
    }
}

} // namespace webhttp
