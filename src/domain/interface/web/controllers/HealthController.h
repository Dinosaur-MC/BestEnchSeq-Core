#pragma once
#include "domain/interface/components/http/HttpController.h"

namespace web {

/// GET /health — liveness probe. No BesqContext needed.
class HealthController : public HttpController<HealthController> {
public:
    using Self = HealthController;

    static constexpr auto route_defs() {
        return std::array{ BESQ_ROUTE(Get, "/health", health) };
    }

    Response health();
};

} // namespace web
