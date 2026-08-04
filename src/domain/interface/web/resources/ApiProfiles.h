#pragma once
#include "common/io/json.h"
#include <string>

class BesqContext;

/// /api/profile[...] — profile list + actions + per-name registry CRUD.
///
///   GET    /api/profile                        → list
///   POST   /api/profile   {action:...}         → activate/fork/merge/remove/publish/create
///   GET    /api/profile/{id}/ench|equip|tag    → read one registry
///   POST   /api/profile/{id}/ench|equip|tag    → add (body = entry)
///   DELETE /api/profile/{id}/ench|equip|tag/{name} → remove
struct ApiProfiles {
    static std::string handle_list(const BesqContext& ctx);
    static std::string handle_action(BesqContext& ctx, const Json& body);
    static std::string handle_read(const BesqContext& ctx, const std::string& profile, const std::string& kind);
    static std::string handle_add(BesqContext& ctx, const std::string& profile, const std::string& kind, const Json& body);
    static std::string
    handle_remove(BesqContext& ctx, const std::string& profile, const std::string& kind, const std::string& name);
};
