// src/domain/interface/components/http/Router.cpp
#include "Router.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include <string>

namespace web {

bool Router::match_segments(std::string_view pattern, std::string_view path, PathParams& out) {
    out.kv.clear();
    size_t pi = 0, si = 0;
    while (pi < pattern.size() && si < path.size()) {
        if (pattern[pi] == '{') {
            auto close = pattern.find('}', pi);
            if (close == std::string_view::npos) return false;
            if (close == pi + 1) return false;
            auto slash = path.find('/', si);
            size_t end = slash == std::string_view::npos ? path.size() : slash;
            if (end == si) return false;
            out.kv.emplace_back(std::string(pattern.substr(pi + 1, close - pi - 1)),
                                percent_decode(path.substr(si, end - si)));
            pi = close + 1;
            si = end;
        } else {
            if (pattern[pi] != path[si]) return false;
            ++pi; ++si;
        }
    }
    return pi == pattern.size() && si == path.size();
}

HttpResponse Router::dispatch(const HttpRequest& req) {
    try {
        bool path_exists = false;
        std::string allow;
        for (const auto& ctrl : _controllers) {
            for (const auto& def : ctrl->routes()) {
                PathParams pp;
                if (!match_segments(def.pattern, req.path, pp)) continue;
                path_exists = true;
                if (def.method == req.method ||
                    (req.method == Method::Head && def.method == Method::Get)) {
                    HttpResponse r = def.invoke(ctrl.get(), req, pp);
                    // HEAD：语义同 GET 但无响应体；显式 Content-Length 保留 GET 的
                    // 字节长度（to_bytes 对已有显式 Content-Length 不再自动追加）。
                    if (req.method == Method::Head && !r.is_stream) {
                        r.headers.emplace_back("Content-Length", std::to_string(r.body.size()));
                        r.body.clear();
                    }
                    return r;
                }
                allow += method_name(def.method);
                allow += ", ";
            }
        }
        if (path_exists) {
            if (!allow.empty()) allow.resize(allow.size() - 2);
            return HttpResponse::method_not_allowed(allow);
        }
        return HttpResponse::not_found();
    } catch (const WebHttpError& e) {
        // 4xx 属正常控制流（404/405/409 等），DEBUG 级进日志视图供排查。
        LOG_DEBUG("%s %s -> %d %s", method_name(req.method), req.path.c_str(), e.status,
                  e.code.c_str());
        return HttpResponse::error(e.status, e.code, e.what());
    } catch (const JsonException&) {
        LOG_DEBUG("%s %s -> %d %s", method_name(req.method), req.path.c_str(), 400,
                  "INVALID_BODY");
        return HttpResponse::error(400, "INVALID_BODY", "invalid request body");
    } catch (const std::exception& e) {
        LOG_ERROR("uncaught exception dispatching %s %s: %s", method_name(req.method),
                  req.path.c_str(), e.what());
        // 500 envelope 不透出内部细节（异常文本仅服务端日志可见）。
        return HttpResponse::internal_error("internal server error");
    }
}

} // namespace web
