#include "HttpServer.h"

#include <chrono>
#include <thread>

namespace webhttp {

bool HttpServer::start(const std::string& host, uint16_t port) {
    return _listener.listen(host, port);
}

void HttpServer::set_handler(const std::string& method, const std::string& pattern, Handler h) {
    _routes.push_back(Route{method, pattern, std::move(h)});
}

void HttpServer::set_fallback(Handler h) {
    _fallback = std::move(h);
}

HttpResponse HttpServer::dispatch(const HttpRequest& req) {
    for (const auto& route : _routes) {
        if (route.method != req.method) continue;
        std::vector<std::string> params;
        if (match_pattern(route.pattern, req.path, params))
            return route.handler(req);   // params not needed by M0.3 handlers
    }
    if (_fallback) return _fallback(req);
    return HttpResponse::json(404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}");
}

void HttpServer::handle_connection(int fd) {
    std::string received;
    for (int i = 0; i < 64; ++i) {  // bound the loop for a single request
        std::string chunk;
        int n = sock_recv(fd, chunk, 64 * 1024, 5000);
        if (n <= 0) return;  // closed / timeout / error
        received += chunk;
        HttpRequest req;
        size_t consumed = 0;
        auto pr = HttpParser::parse(received, consumed, req);
        if (pr == ParseResult::Incomplete) continue;
        if (pr == ParseResult::BadRequest) {
            sock_send(fd, HttpResponse::json(400, "Bad Request",
                                             "{\"ok\":false,\"error\":\"bad request\"}").to_bytes(), 3000);
            return;
        }
        auto resp = dispatch(req);
        sock_send(fd, resp.to_bytes(), 5000);
        return;
    }
}

void HttpServer::run() {
    _running.store(true, std::memory_order_release);
    while (_running.load(std::memory_order_acquire)) {
        // select() with a short timeout so stop() is honored promptly even
        // while no client is connecting (a bare blocking accept() would never
        // return, hanging run() — and therefore server_thread.join() — after
        // stop()).
        if (_listener.wait_ready(100) <= 0) {
            if (!_running.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // no busy-spin
            continue;  // timeout → re-check _running
        }
        int c = _listener.accept();
        if (c < 0) {
            if (!_running.load(std::memory_order_acquire)) break;
            continue;
        }
        set_send_timeout(c, 5000);  // bound response writes; a non-reading peer can't wedge run()
        handle_connection(c);
        sock_close(c);
    }
}

} // namespace webhttp
