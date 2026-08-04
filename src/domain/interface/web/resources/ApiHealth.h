#pragma once
#include <string>

/// GET /health — liveness probe. No BesqContext needed.
struct ApiHealth {
    static std::string handle();
};
