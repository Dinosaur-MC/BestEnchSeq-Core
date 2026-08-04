#pragma once
#include "HttpCommon.h"
#include "HttpParser.h"
#include "Socket.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace webhttp {

/// Minimal single-threaded HTTP/1.1 server for the localhost GUI.
///
/// Accept loop is blocking; one connection is fully handled before the next is
/// accepted. Route patterns support `{name}` path segments, e.g.
/// `/api/calculator/{id}`. Everything unmatched goes to the fallback handler
/// (static resources / 404).
///
/// Per-connection work is bounded: each read waits up to 5s and a request gets
/// at most 64 reads, so a stalled client ties up the single accept loop for a
/// finite time (a silent client ~5s, a trickling one up to ~5.3 min) rather
/// than indefinitely. Writes are bounded too (SO_SNDTIMEO 5s), so a peer that
/// stops reading cannot wedge run() forever.
class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    /// Bind+listen. port 0 → OS-assigned.
    bool start(const std::string& host, uint16_t port);
    uint16_t port() const noexcept { return _listener.bound_port(); }

    /// Register a route. `pattern` uses `{seg}` placeholders.
    void set_handler(const std::string& method, const std::string& pattern, Handler h);
    /// Fallback for unmatched method+path.
    void set_fallback(Handler h);

    /// Run the accept loop until stop() (blocking).
    void run();
    void stop() noexcept { _running.store(false, std::memory_order_release); }

private:
    HttpResponse dispatch(const HttpRequest& req);
    void handle_connection(int fd);
    static bool match_pattern(const std::string& pattern, const std::string& path,
                              std::vector<std::string>& params);

    TcpListener _listener;
    struct Route {
        std::string method;
        std::string pattern;
        Handler handler;
    };
    std::vector<Route> _routes;
    Handler _fallback;
    std::atomic<bool> _running{false};
};

} // namespace webhttp
