#include "HttpCommon.h"
#include "common/io/json.h"
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace web {

const char* method_name(Method m) {
    switch (m) {
        case Method::Get: return "GET";
        case Method::Head: return "HEAD";
        case Method::Post: return "POST";
        case Method::Put: return "PUT";
        case Method::Patch: return "PATCH";
        case Method::Delete: return "DELETE";
    }
    return "GET";
}

bool method_from(std::string_view s, Method& out) {
    if (s == "GET") out = Method::Get;
    else if (s == "HEAD") out = Method::Head;
    else if (s == "POST") out = Method::Post;
    else if (s == "PUT") out = Method::Put;
    else if (s == "PATCH") out = Method::Patch;
    else if (s == "DELETE") out = Method::Delete;
    else return false;
    return true;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexval(s[i + 1]), lo = hexval(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

QueryParams parse_query(std::string_view qs) {
    QueryParams q;
    size_t pos = 0;
    while (pos <= qs.size()) {
        auto amp = qs.find('&', pos);
        std::string_view part = amp == std::string_view::npos ? qs.substr(pos) : qs.substr(pos, amp - pos);
        if (!part.empty()) {
            auto eq = part.find('=');
            std::string k = eq == std::string_view::npos ? std::string(part) : std::string(part.substr(0, eq));
            std::string v = eq == std::string_view::npos ? "" : percent_decode(part.substr(eq + 1));
            q.kv[std::move(k)] = std::move(v);
        }
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return q;
}

const char* mime_for(std::string_view ext) {
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js" || ext == ".mjs") return "text/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".txt") return "text/plain";
    if (ext == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

std::string PathParams::get(const std::string& name) const {
    for (const auto& [k, v] : kv) if (k == name) return v;
    throw std::out_of_range("path param: " + name);
}
std::string_view PathParams::get_sv(const std::string& name) const {
    for (const auto& [k, v] : kv) if (k == name) return v;
    throw std::out_of_range("path param: " + name);
}
bool PathParams::has(const std::string& name) const {
    for (const auto& [k, v] : kv) if (k == name) return true;
    return false;
}

static bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string HttpRequest::header(const std::string& name) const {
    for (const auto& [k, v] : headers) if (ieq(k, name)) return v;
    return "";
}

std::string HttpResponse::to_bytes() const {
    std::string out;
    out += "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    out += "Content-Type: " + content_type + "\r\n";
    // 已有显式 Content-Length 头时不再自动追加（如 HEAD 的 r.body.clear() 场景），
    // 避免线上出现两个互相矛盾的 Content-Length（RFC 9112 视为请求走私）。
    bool has_content_length = false;
    for (const auto& kv : headers)
        if (ieq(kv.first, "Content-Length")) { has_content_length = true; break; }
    if (!has_content_length)
        out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    for (const auto& [k, v] : headers) {
        out += k;
        out += ": ";
        out += v;
        out += "\r\n";
    }
    if (is_stream) out += "Transfer-Encoding: chunked\r\n";
    else out += "Connection: keep-alive\r\n";   // HTTP/1.1 默认 keep-alive；是否关闭由 Connection 状态机决定
    out += "\r\n";
    out += body;
    return out;
}

std::string HttpResponse::header_value(const std::string& name) const {
    for (const auto& [k, v] : headers)
        if (ieq(k, name)) return v;
    return "";
}

HttpResponse HttpResponse::json(int status, const std::string& reason, const std::string& body) {
    return HttpResponse{status, reason, "application/json", {}, body, false};
}
HttpResponse HttpResponse::created(const std::string& location, const std::string& body) {
    HttpResponse r = json(201, "Created", body);
    r.headers.emplace_back("Location", location);
    return r;
}
HttpResponse HttpResponse::accepted(const std::string& location, const std::string& body) {
    HttpResponse r = json(202, "Accepted", body);
    r.headers.emplace_back("Location", location);
    return r;
}
HttpResponse HttpResponse::no_content() { return HttpResponse{204, "No Content", "", {}, "", false}; }
HttpResponse HttpResponse::method_not_allowed(const std::string& allow) {
    HttpResponse r = json(405, "Method Not Allowed", R"({"ok":false,"error":{"code":"METHOD_NOT_ALLOWED","message":"method not allowed"}})");
    r.headers.emplace_back("Allow", allow);
    return r;
}

namespace {
/// 状态码 → 原因短语。仅供 error() 工厂内部使用（项目无全局 reason_for）。
const char* reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
    }
    return "Error";
}
} // namespace

HttpResponse HttpResponse::error(int status, const std::string& code, const std::string& message) {
    Json o = Json::object();
    Json e = Json::object();
    e["code"] = Json(code);
    e["message"] = Json(message);
    o["ok"] = Json(false);
    o["error"] = e;
    return json(status, reason_phrase(status), o.to_string());
}
// 便捷工厂委托 error()：not_found→404, bad_request→400, conflict→409, internal_error→500
HttpResponse HttpResponse::not_found() { return error(404, "NOT_FOUND", "not found"); }
HttpResponse HttpResponse::bad_request(const std::string& code, const std::string& message) {
    return error(400, code, message);
}
HttpResponse HttpResponse::conflict(const std::string& code, const std::string& message) {
    return error(409, code, message);
}
HttpResponse HttpResponse::internal_error(const std::string& message) {
    return error(500, "INTERNAL_ERROR", message);
}

} // namespace web
