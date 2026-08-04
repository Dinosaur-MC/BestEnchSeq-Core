#pragma once
#include <string>

class BesqContext;

/// /api/algorithm — list registered strategies with metadata, query one,
/// load a plugin directory.
struct ApiAlgorithm {
    static std::string handle_list(const BesqContext& ctx);
    static std::string handle_get(const BesqContext& ctx, const std::string& name);
    static std::string handle_load(BesqContext& ctx, const std::string& dir);
};
