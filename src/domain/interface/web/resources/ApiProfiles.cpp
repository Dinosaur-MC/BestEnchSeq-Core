#include "ApiProfiles.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Profile.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebHttpError.h"
#include <algorithm>

namespace {

void validate_kind(const std::string& kind) {
    if (kind != "ench" && kind != "equip" && kind != "tag")
        throw webhttp::WebHttpError(404, "unknown registry kind: " + kind);
}

Json entry_to_json(const EnchInfo& e) {
    return e.to_json();
}
Json entry_to_json(const Equipment& e) {
    return e.to_json();
}
Json entry_to_json(const EquipmentTag& e) {
    return e.to_json();
}

template <typename Entry> Json registries_to_json(const std::vector<Entry>& entries) {
    Json arr = Json::array();
    for (const auto& e : entries)
        arr.push_back(entry_to_json(e));
    return arr;
}

template <typename Registry> Json registry_json(const Registry& reg) {
    std::vector<typename Registry::value_type> entries;
    for (const auto& e : reg)
        entries.push_back(e);
    // stable ordering by id
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.id.str() < b.id.str(); });
    return registries_to_json(entries);
}

} // namespace

std::string ApiProfiles::handle_list(const BesqContext& ctx) {
    Json arr = Json::array();
    for (const auto& name : ctx.list_profiles())
        arr.push_back(Json(name));
    Json o = Json::object();
    o["profiles"] = arr;
    o["active"] = Json(ctx.active_profile());
    return o.to_string();
}

std::string ApiProfiles::handle_action(BesqContext& ctx, const Json& body) {
    Json ok = Json::object();
    try {
        auto action = body["action"].as<std::string>();
        if (action == "activate") {
            ctx.activate_profile(body["name"].as<std::string>());
        } else if (action == "fork") {
            ctx.fork_profile(body["source"].as<std::string>(), body["dest"].as<std::string>());
        } else if (action == "merge") {
            ctx.merge_profile(body["source"].as<std::string>(), body["dest"].as<std::string>());
        } else if (action == "remove") {
            std::string name = body["name"].as<std::string>();
            bool exists = false;
            for (const auto& n : ctx.list_profiles())
                if (n == name) {
                    exists = true;
                    break;
                }
            if (!exists)
                throw webhttp::WebHttpError(404, "profile not found: " + name);
            ctx.remove_profile(name);
        } else if (action == "publish") {
            std::string version = body.has("version") ? body["version"].as<std::string>() : "";
            std::string tag = body.has("tag") ? body["tag"].as<std::string>() : "";
            std::string path = body["path"].as<std::string>();
            if (!ctx.publish_profile(body["name"].as<std::string>(), version, tag, path))
                throw webhttp::WebHttpError(400, "publish failed for " + body["name"].as<std::string>());
        } else if (action == "create") {
            ctx.fork_profile(body["source"].as<std::string>(), body["dest"].as<std::string>());
        } else {
            throw webhttp::WebHttpError(400, "unknown action: " + action);
        }
    } catch (const JsonException&) {
        // Missing/non-string `action` (or a required action parameter) → 400,
        // not a raw JsonException. The intentional WebHttpError throws above
        // are unrelated types and pass through untouched.
        throw webhttp::WebHttpError(400, "invalid action body");
    }
    ok["ok"] = Json(true);
    return ok.to_string();
}

std::string ApiProfiles::handle_read(const BesqContext& ctx, const std::string& profile, const std::string& kind) {
    validate_kind(kind);
    const Profile* p = nullptr;
    try {
        p = &ctx.profile(profile); // facade throws std::runtime_error when unknown
    } catch (const std::exception&) {
        // Convert to the wire type so the dispatcher emits a 404 envelope
        // (unknown resource), not a generic 400.
        throw webhttp::WebHttpError(404, "unknown profile: " + profile);
    }
    Json o = Json::object();
    if (kind == "ench")
        o["enchantments"] = registry_json(p->ench());
    else if (kind == "equip")
        o["equipments"] = registry_json(p->eq());
    else
        o["tags"] = registry_json(p->tags());
    return o.to_string();
}

std::string ApiProfiles::handle_add(BesqContext& ctx, const std::string& profile, const std::string& kind, const Json& body) {
    validate_kind(kind);
    bool ok = false;
    try {
        if (kind == "ench") {
            EnchInfo info;
            info.from_json(body);
            ok = ctx.add_enchantment_to(profile, info);
        } else if (kind == "equip") {
            Equipment eq;
            eq.from_json(body);
            ok = ctx.add_equipment_to(profile, eq);
        } else {
            EquipmentTag tag;
            tag.from_json(body);
            ok = ctx.add_tag_to(profile, tag);
        }
    } catch (const std::exception&) {
        // from_json() surfaces a malformed entry body as ds::ValidationError (a
        // std::runtime_error subclass) — translate to a 400, not a raw
        // exception. validate_kind() (404) ran above and is not swallowed.
        throw webhttp::WebHttpError(400, "invalid entry body");
    }
    if (!ok)
        throw webhttp::WebHttpError(409, "entry exists or violates validation");
    Json o = Json::object();
    o["ok"] = Json(true);
    return o.to_string();
}

std::string
ApiProfiles::handle_remove(BesqContext& ctx, const std::string& profile, const std::string& kind, const std::string& name) {
    validate_kind(kind);
    bool ok = false;
    if (kind == "ench")
        ok = ctx.remove_enchantment_from(profile, NSID(name));
    else if (kind == "equip")
        ok = ctx.remove_equipment_from(profile, NSID(name));
    else
        ok = ctx.remove_tag_from(profile, NSID(name));
    if (!ok)
        throw webhttp::WebHttpError(404, "entry not found: " + name);
    Json o = Json::object();
    o["ok"] = Json(true);
    return o.to_string();
}
