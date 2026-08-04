#pragma once
#include <string>
#include <utility>
#include <vector>

namespace webhttp {

/// Request-line + headers + (optional) body parsed from a raw HTTP/1.1 stream.
struct HttpRequest {
    std::string method;                       // GET / POST / PUT / DELETE
    std::string path;                         // path portion before '?'; not URL-decoded
    std::string query;                        // raw query string (no leading '?')
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    /// Case-insensitive header lookup; returns "" when absent.
    std::string header(const std::string& name) const {
        for (const auto& [k, v] : headers) {
            if (k.size() != name.size()) continue;
            bool same = true;
            for (size_t i = 0; i < name.size(); ++i) {
                char a = k[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b) { same = false; break; }
            }
            if (same) return v;
        }
        return "";
    }
};

/// Outbound response. `to_bytes()` renders the full HTTP/1.1 wire form.
struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string content_type = "application/json";
    std::string body;

    std::string to_bytes() const {
        std::string out;
        out += "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
        out += "Content-Type: " + content_type + "\r\n";
        out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        out += "Connection: close\r\n";
        out += "\r\n";
        out += body;
        return out;
    }

    static HttpResponse json(int status, const std::string& reason, const std::string& body) {
        return HttpResponse{status, reason, "application/json", body};
    }
};

/// Shared `{param}` segment matcher used by HttpServer and WebModule.
inline bool match_pattern(const std::string& pattern, const std::string& path,
                          std::vector<std::string>& params) {
    params.clear();
    size_t pi = 0, si = 0;
    while (pi < pattern.size() && si < path.size()) {
        if (pattern[pi] == '{') {
            auto close = pattern.find('}', pi);
            if (close == std::string::npos) return false;
            if (close == pi + 1) return false;   // empty {} placeholder not allowed
            auto slash = path.find('/', si);
            size_t end = slash == std::string::npos ? path.size() : slash;
            if (end == si) return false;
            params.push_back(path.substr(si, end - si));
            pi = close + 1;
            si = end;
        } else {
            if (pattern[pi] != path[si]) return false;
            ++pi; ++si;
        }
    }
    return pi == pattern.size() && si == path.size();
}

} // namespace webhttp
