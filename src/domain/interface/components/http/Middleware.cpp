#include "Middleware.h"

namespace web {

namespace {

/// X-Forwarded-For 最右条目（去 OWS）；缺失/空白 → 空串。
std::string rightmost_xff(const HttpRequest& req) {
    const std::string xff = req.header("X-Forwarded-For");
    if (xff.empty())
        return "";
    const size_t comma = xff.rfind(',');
    const std::string_view last = comma == std::string::npos ? std::string_view(xff) : std::string_view(xff).substr(comma + 1);
    const size_t b = last.find_first_not_of(" \t");
    const size_t e = last.find_last_not_of(" \t");
    if (b == std::string_view::npos || e == std::string_view::npos)
        return "";
    return std::string(last.substr(b, e - b + 1));
}

} // namespace

std::string client_addr(const HttpRequest& req, const ClientAddrPolicy& policy) {
    if (req.remote_addr.empty())
        return "";
    if (policy.trust_forwarded) {
        bool trusted_peer = false;
        for (const auto& p : policy.trusted_proxies)
            if (p == req.remote_addr) {
                trusted_peer = true;
                break;
            }
        if (trusted_peer) {
            const std::string fwd = rightmost_xff(req);
            if (!fwd.empty())
                return fwd;
        }
    }
    return req.remote_addr;
}

} // namespace web
