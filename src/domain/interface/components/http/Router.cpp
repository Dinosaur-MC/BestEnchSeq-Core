// src/domain/interface/components/http/Router.cpp
#include "Router.h"
#include "common/io/json.h"
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
                if (def.method == req.method)
                    return def.invoke(ctrl.get(), req, pp);
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
        return HttpResponse::error(e.status, e.code, e.what());
    } catch (const JsonException&) {
        return HttpResponse::error(400, "INVALID_BODY", "invalid request body");
    } catch (const std::exception& e) {
        return HttpResponse::internal_error(e.what());
    }
}

} // namespace web
