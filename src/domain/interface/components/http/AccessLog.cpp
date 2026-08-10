#include "AccessLog.h"
#include "common/log/Logger.h"
#include <chrono>
#include <cstdio>
#include <string>

namespace web {

namespace {

/// 日志消毒：客户端可控字节（path/referer/UA）可能含控制字符 → 替换为 '_'
///（与 Connection::sanitize_for_log 同款，防日志注入）。
std::string sanitize_for_log(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20) c = '_';
    return out;
}

/// [dd/Mon/yyyy:HH:mm:ss +zzzz]（CLF 惯例；线程安全 localtime 变体）。
std::string clf_timestamp() {
    const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "[%d/%b/%Y:%H:%M:%S %z]", &tm);
    return buf;
}

/// 引用或 "-"（缺省字段惯例；CLF 中缺省字段为带引号的 "-"）。
std::string quote_or_dash(const std::string& s) {
    return s.empty() ? "\"-\"" : "\"" + sanitize_for_log(s) + "\"";
}

} // namespace

std::string AccessLogger::request_line(const HttpRequest& req) {
    return std::string(method_name(req.method)) + " " + req.path + " " +
           (req.version.empty() ? "HTTP/1.1" : req.version);
}

void AccessLogger::log_line(const HttpRequest& req, const HttpResponse& resp) {
    std::string bytes = "-";
    if (!resp.is_stream && !resp.body.empty())
        bytes = std::to_string(resp.body.size());
    std::string line = (req.remote_addr.empty() ? "-" : req.remote_addr) + " - - " +
                       clf_timestamp() + " \"" + sanitize_for_log(request_line(req)) + "\" " +
                       std::to_string(resp.status) + " " + bytes + " " +
                       quote_or_dash(req.header("Referer")) + " " +
                       quote_or_dash(req.header("User-Agent"));
    Logger::instance().info(std::move(line));
}

HttpResponse AccessLogger::operator()(const HttpRequest& req, const Next& next) {
    try {
        HttpResponse resp = next(req);
        log_line(req, resp);
        return resp;
    } catch (...) {
        // 防御兜底：Router 已把控制器异常映射为响应，正常不会走到；记录 500 后重抛。
        HttpResponse err = HttpResponse::internal_error("middleware chain threw");
        log_line(req, err);
        throw;
    }
}

Middleware make_access_logger(ClientAddrPolicy policy) {
    return AccessLogger(std::move(policy));
}

} // namespace web
