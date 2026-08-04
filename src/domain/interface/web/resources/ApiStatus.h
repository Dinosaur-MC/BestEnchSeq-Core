#pragma once
#include <string>

class BesqContext;

/// GET /api/status — session snapshot (active profile, algorithm count,
/// active solve, memory, uptime).
struct ApiStatus {
    static std::string handle(const BesqContext& ctx);
};
