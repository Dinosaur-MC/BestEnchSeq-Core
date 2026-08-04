#pragma once
#include "domain/interface/web/http/HttpCommon.h"
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/resources/ApiProfiles.h"
#include <map>
#include <string>
#include <vector>

class BesqContext;

namespace webhttp {

/// A static asset served by the GUI host (embedded bytes, or read from disk
/// in dev mode).
struct StaticResource {
    std::string content_type;
    std::string content;
};

/// The interface-domain Web module: a pure translation layer between HTTP
/// requests and BesqContext, exactly per interface-domain-design.md §4.
///
/// Owns a WebSolveService (the async task table) and the static-resource
/// table. Errors map to `{ok:false,error}` envelopes with the status codes
/// from the design spec: 400 input / 404 unknown / 409 conflict / 500 internal.
class WebModule {
public:
    explicit WebModule(BesqContext& ctx);

    /// Inject the SPA assets (path → resource). Dev mode: read from disk.
    void set_static_resources(std::map<std::string, StaticResource> resources);

    /// Dispatch one HTTP request to a resource handler. Never throws; returns
    /// a response (200/400/404/409/500) with a JSON envelope on errors.
    HttpResponse dispatch(const std::string& method, const std::string& path,
                          const std::string& body);

private:
    struct Route {
        std::string method;
        std::string pattern;
        std::string (*handler)(WebModule&, const std::vector<std::string>&, const std::string&);
    };

    BesqContext& _ctx;
    WebSolveService _solve;
    std::map<std::string, StaticResource> _static;
    std::vector<Route> _routes;
};

} // namespace webhttp
