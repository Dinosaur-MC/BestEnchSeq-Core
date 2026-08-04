#pragma once
#include "domain/interface/components/http/HttpController.h"

class BesqContext;

namespace webhttp {
class WebSolveService;
}

namespace web {

/// GET/POST /api/algorithms — list registered strategies, query one's
/// metadata, hot-load a plugin directory, unload a plugin.
///
/// Replaces the old ApiAlgorithm resource (web-http-modernize §6.2). The
/// unload handler is gated on `WebSolveService::has_active()`: a running solve
/// may be holding the plugin's executor, so unloading mid-solve is refused with
/// 409 TASK_ACTIVE.
class AlgorithmController : public HttpController<AlgorithmController> {
public:
    using Self = AlgorithmController;

    AlgorithmController(BesqContext& ctx, webhttp::WebSolveService& svc)
        : _ctx(ctx), _svc(svc) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get,  "/api/algorithms",        list),
            BESQ_ROUTE(Get,  "/api/algorithms/{name}", detail),
            BESQ_ROUTE(Post, "/api/algorithms/load",   load),
            BESQ_ROUTE(Post, "/api/algorithms/unload", unload),
        };
    }

    Response list(const HttpRequest&);
    Response detail(const HttpRequest&, const PathParams&);
    Response load(const HttpRequest&, const PathParams&, const Json&);
    Response unload(const HttpRequest&, const PathParams&, const Json&);

private:
    BesqContext& _ctx;
    webhttp::WebSolveService& _svc;
};

} // namespace web
