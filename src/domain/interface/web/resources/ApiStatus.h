#pragma once
#include <string>

class BesqContext;

/// GET /api/status — session snapshot (active profile, profile count,
/// algorithm count, active solve, uptime).
struct ApiStatus {
    static std::string handle(const BesqContext& ctx);
};
