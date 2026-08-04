#include "ApiProfiles.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Profile.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebHttpError.h"
#include <algorithm>

namespace {

bool profile_exists(const BesqContext& ctx, const std::string& name) {
    for (const auto& n : ctx.list_profiles())
        if (n == name)
            return true;
    return false;
}

void validate_kind(const std::string& kind) {
    if (kind != "ench" && kind != "equip" && kind != "tag")
        throw webhttp::WebHttpError(404, "unknown registry kind: " + kind);
}

template <typename Entry> Json entries_to_json(const std::vector<Entry>& entries) {
    Json arr = Json::array();
    for (const auto& e : entries)
        arr.push_back(e.to_json());
    return arr;
}

template <typename Registry> Json registry_json(const Registry& reg) {
    std::vector<typename Registry::value_type> entries;
    for (const auto& e : reg)
        entries.push_back(e);
    // stable ordering by id
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.id.str() < b.id.str(); });
    return entries_to_json(entries);
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
    try {
        auto action = body["action"].as<std::string>();
        if (action == "activate") {
            auto name = body["name"].as<std::string>();
            if (!profile_exists(ctx, name))
                throw webhttp::WebHttpError(404, "profile not found: " + name);
            ctx.activate_profile(name);
        } else if (action == "fork" || action == "create") {
            auto source = body["source"].as<std::string>();
            auto dest = body["dest"].as<std::string>();
            if (!profile_exists(ctx, source))
                throw webhttp::WebHttpError(404, "source profile not found: " + source);
            if (profile_exists(ctx, dest))
                throw webhttp::WebHttpError(409, "destination profile already exists: " + dest);
            ctx.fork_profile(source, dest);
        } else if (action == "merge") {
            auto source = body["source"].as<std::string>();
            auto dest = body["dest"].as<std::string>();
            if (!profile_exists(ctx, source))
                throw webhttp::WebHttpError(404, "source profile not found: " + source);
            if (!profile_exists(ctx, dest))
                throw webhttp::WebHttpError(404, "destination profile not found: " + dest);
            ctx.merge_profile(source, dest);
        } else if (action == "remove") {
            auto name = body["name"].as<std::string>();
            if (!profile_exists(ctx, name))
                throw webhttp::WebHttpError(404, "profile not found: " + name);
            ctx.remove_profile(name);
        } else if (action == "publish") {
            auto name = body["name"].as<std::string>();
            if (!profile_exists(ctx, name))
                throw webhttp::WebHttpError(404, "profile not found: " + name);
            std::string version = body.has("version") ? body["version"].as<std::string>() : "";
            std::string tag = body.has("tag") ? body["tag"].as<std::string>() : "";
            std::string path = body["path"].as<std::string>();
            if (!ctx.publish_profile(name, version, tag, path))
                throw webhttp::WebHttpError(400, "publish failed for " + name);
        } else {
            throw webhttp::WebHttpError(400, "unknown action: " + action);
        }
    } catch (const webhttp::WebHttpError&) {
        throw; // intentional status-carrying errors pass through
    } catch (const JsonException&) {
        throw webhttp::WebHttpError(400, "invalid action body");
    } catch (const std::exception& e) {
        throw webhttp::WebHttpError(500, std::string("internal error: ") + e.what());
    }

    Json ok = Json::object();
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
    // raw own-data; dependency content not merged — this is an editor read, by
    // design (see BesqContext::profile()).
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
    if (!profile_exists(ctx, profile))
        throw webhttp::WebHttpError(404, "profile not found: " + profile);
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
    } catch (const webhttp::WebHttpError&) {
        throw; // intentional status-carrying errors pass through
    } catch (const std::exception&) {
        // from_json() surfaces a malformed entry body as ds::ValidationError (a
        // std::runtime_error subclass) — translate to a 400, not a raw
        // exception. validate_kind() (404) and the 409 below stay OUTSIDE the
        // try so they are never re-mapped.
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
