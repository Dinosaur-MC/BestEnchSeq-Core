#include "ProfilesController.h"
#include "domain/interface/BesqContext.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "common/io/json.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace web {

namespace {

/// `{"ok":true}` — the payload shape for action endpoints (§6.2).
Json ok_json() {
    Json o = Json::object();
    o["ok"] = Json(true);
    return o;
}

/// Stable ordering by id — mirrors the old ApiProfiles::registry_json.
template <typename Registry>
Json registry_json(const Registry& reg) {
    std::vector<typename Registry::value_type> entries;
    for (const auto& e : reg)
        entries.push_back(e);
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.id.str() < b.id.str(); });
    Json arr = Json::array();
    for (const auto& e : entries)
        arr.push_back(e.to_json());
    return arr;
}

/// 404 on an unknown profile (all profile-scoped handlers).
void require_profile(const BesqContext& ctx, const std::string& key) {
    if (!ctx.profile_exists(key))
        throw WebHttpError(404, "PROFILE_NOT_FOUND", "profile not found: " + key);
}

/// Parse a `{name}` path segment into an NSID; an unparseable segment is
/// treated as "not found" (404), never a 500.
NSID path_nsid(const std::string& name) {
    try {
        return NSID(name);
    } catch (const std::exception&) {
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "entry not found: " + name);
    }
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Profile collection / item
// ══════════════════════════════════════════════════════════════════════════

Response ProfilesController::list(const HttpRequest&) {
    std::lock_guard<std::mutex> lock(_gate);
    Json arr = Json::array();
    for (const auto& name : _ctx.list_profiles())
        arr.push_back(Json(name));
    Json o = Json::object();
    o["profiles"] = arr;
    o["active"] = Json(_ctx.active_profile());
    return Response::json(200, "OK", o.to_string());
}

Response ProfilesController::create(const HttpRequest&, const PathParams&, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "create body must be a JSON object");
    std::string source, dest;
    try {
        source = body["source"].as<std::string>();
        dest = body["dest"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "create body requires 'source' and 'dest' strings");
    }
    if (dest.empty())
        throw WebHttpError(400, "INVALID_FIELD", "dest must not be empty");
    if (!_ctx.profile_exists(source))
        throw WebHttpError(404, "PROFILE_NOT_FOUND", "source profile not found: " + source);
    if (_ctx.profile_exists(dest))
        throw WebHttpError(409, "PROFILE_EXISTS", "destination profile already exists: " + dest);
    _ctx.fork_profile(source, dest);
    return Response::created("/api/profiles/" + dest, ok_json().to_string());
}

Response ProfilesController::read(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    const ProfileMeta meta = _ctx.profile_metadata(key);
    Json o = Json::object();
    o["name"] = Json(meta.name);
    Json deps = Json::array();
    for (const auto& d : meta.dependencies)
        deps.push_back(Json(d));
    o["dependencies"] = deps;
    o["version"] = Json(meta.version);
    o["release_tag"] = Json(meta.release_tag);
    o["is_root"] = Json(meta.is_root);
    o["ench_count"] = Json(static_cast<int64_t>(meta.ench_count));
    o["eq_count"] = Json(static_cast<int64_t>(meta.eq_count));
    o["tag_count"] = Json(static_cast<int64_t>(meta.tag_count));
    o["format"] = Json(meta.format);
    return Response::json(200, "OK", o.to_string());
}

Response ProfilesController::update(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "profile update body must be a JSON object");

    if (body.has("dependencies")) {
        const Json deps = body["dependencies"];
        if (deps.type() != JsonType::Array)
            throw WebHttpError(400, "INVALID_FIELD", "dependencies must be an array of strings");
        for (const auto& d : deps.as_array())
            if (d.type() != JsonType::String)
                throw WebHttpError(400, "INVALID_FIELD", "dependencies must be an array of strings");
        std::vector<std::string> dep_list;
        dep_list.reserve(deps.as_array().size());
        for (const auto& d : deps.as_array())
            dep_list.push_back(d.as<std::string>());
        // Profile existence is already guarded by require_profile above, so a
        // false return here can only mean the change would create a dependency
        // cycle (ProfileManager::set_dependencies rejects without mutating).
        if (!_ctx.set_dependencies(key, std::move(dep_list)))
            throw WebHttpError(409, "DEPENDENCY_CYCLE", "dependency change rejected (cycle): " + key);
    }

    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::remove(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    _ctx.remove_profile(key);
    return Response::no_content();
}

// ══════════════════════════════════════════════════════════════════════════
// Actions
// ══════════════════════════════════════════════════════════════════════════

Response ProfilesController::activate(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    _ctx.activate_profile(key);
    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::fork(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "fork body must be a JSON object");
    std::string dest;
    try {
        dest = body["dest"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "fork body requires 'dest' string");
    }
    if (dest.empty())
        throw WebHttpError(400, "INVALID_FIELD", "dest must not be empty");
    if (_ctx.profile_exists(dest))
        throw WebHttpError(409, "PROFILE_EXISTS", "destination profile already exists: " + dest);
    _ctx.fork_profile(key, dest);
    return Response::created("/api/profiles/" + dest, ok_json().to_string());
}

Response ProfilesController::merge(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "merge body must be a JSON object");
    std::string dest;
    try {
        dest = body["dest"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "merge body requires 'dest' string");
    }
    if (!_ctx.profile_exists(dest))
        throw WebHttpError(404, "PROFILE_NOT_FOUND", "destination profile not found: " + dest);
    _ctx.merge_profile(key, dest);
    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::publish(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "publish body must be a JSON object");
    std::string version, tag, path;
    try {
        version = body.has("version") ? body["version"].as<std::string>() : "";
        tag = body.has("tag") ? body["tag"].as<std::string>() : "";
        path = body["path"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "publish body requires 'path' string");
    }
    if (path.empty())
        throw WebHttpError(400, "INVALID_FIELD", "publish body requires non-empty 'path'");
    if (!_ctx.publish_profile(key, version, tag, path))
        throw WebHttpError(400, "PUBLISH_FAILED", "publish failed for " + key);
    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::rename(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (body.type() != JsonType::Object)
        throw WebHttpError(400, "INVALID_FIELD", "rename body must be a JSON object");
    std::string newname;
    try {
        newname = body["name"].as<std::string>();
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_FIELD", "rename body requires 'name' string");
    }
    if (newname.empty())
        throw WebHttpError(400, "INVALID_FIELD", "rename body requires non-empty 'name'");
    if (!_ctx.rename_profile(key, newname))
        throw WebHttpError(409, "PROFILE_EXISTS", "cannot rename: target already exists or invalid");
    return Response::json(200, "OK", ok_json().to_string());
}

// ══════════════════════════════════════════════════════════════════════════
// Enchantment sub-resource
// ══════════════════════════════════════════════════════════════════════════

Response ProfilesController::listEnch(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    return Response::json(200, "OK", registry_json(_ctx.profile(key).ench()).to_string());
}

Response ProfilesController::addEnch(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    EnchInfo info;
    try {
        info.from_json(body);
    } catch (const std::exception&) {
        throw WebHttpError(400, "INVALID_BODY", "invalid enchantment entry body");
    }
    if (info.id.empty())
        throw WebHttpError(400, "INVALID_BODY", "enchantment entry requires a non-empty id");
    if (!_ctx.add_enchantment_to(key, info))
        throw WebHttpError(409, "DUPLICATE_ENTRY", "enchantment exists or violates validation: " + info.id.str());
    return Response::created("/api/profiles/" + key + "/enchantments/" + info.id.str(), ok_json().to_string());
}

Response ProfilesController::readEnch(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    const auto& reg = _ctx.profile(key).ench();
    auto it = reg.find(path_nsid(pp.get("name")));
    if (it == reg.end())
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "enchantment not found: " + pp.get("name"));
    return Response::json(200, "OK", it->to_json().to_string());
}

Response ProfilesController::updateEnch(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    EnchInfo info;
    try {
        info.from_json(body);
    } catch (const std::exception&) {
        throw WebHttpError(400, "INVALID_BODY", "invalid enchantment entry body");
    }
    if (info.id.empty())
        throw WebHttpError(400, "INVALID_BODY", "enchantment entry requires a non-empty id");
    auto pid = path_nsid(pp.get("name"));   // path_nsid throws 404 on an invalid NSID
    if (pid != info.id)
        throw WebHttpError(400, "INVALID_FIELD", "path name must match body id");
    if (!_ctx.update_enchantment_to(key, info))
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "enchantment not found: " + info.id.str());
    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::removeEnch(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (!_ctx.remove_enchantment_from(key, path_nsid(pp.get("name"))))
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "enchantment not found: " + pp.get("name"));
    return Response::no_content();
}

// ══════════════════════════════════════════════════════════════════════════
// Equipment sub-resource
// ══════════════════════════════════════════════════════════════════════════

Response ProfilesController::listEquip(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    return Response::json(200, "OK", registry_json(_ctx.profile(key).eq()).to_string());
}

Response ProfilesController::addEquip(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    Equipment eq;
    try {
        eq.from_json(body);
    } catch (const std::exception&) {
        throw WebHttpError(400, "INVALID_BODY", "invalid equipment entry body");
    }
    if (eq.id.empty())
        throw WebHttpError(400, "INVALID_BODY", "equipment entry requires a non-empty id");
    if (!_ctx.add_equipment_to(key, eq))
        throw WebHttpError(409, "DUPLICATE_ENTRY", "equipment exists or violates validation: " + eq.id.str());
    return Response::created("/api/profiles/" + key + "/equipments/" + eq.id.str(), ok_json().to_string());
}

Response ProfilesController::readEquip(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    const auto& reg = _ctx.profile(key).eq();
    auto it = reg.find(path_nsid(pp.get("name")));
    if (it == reg.end())
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "equipment not found: " + pp.get("name"));
    return Response::json(200, "OK", it->to_json().to_string());
}

Response ProfilesController::updateEquip(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    Equipment eq;
    try {
        eq.from_json(body);
    } catch (const std::exception&) {
        throw WebHttpError(400, "INVALID_BODY", "invalid equipment entry body");
    }
    if (eq.id.empty())
        throw WebHttpError(400, "INVALID_BODY", "equipment entry requires a non-empty id");
    auto pid = path_nsid(pp.get("name"));   // path_nsid throws 404 on an invalid NSID
    if (pid != eq.id)
        throw WebHttpError(400, "INVALID_FIELD", "path name must match body id");
    if (!_ctx.update_equipment_to(key, eq))
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "equipment not found: " + eq.id.str());
    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::removeEquip(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (!_ctx.remove_equipment_from(key, path_nsid(pp.get("name"))))
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "equipment not found: " + pp.get("name"));
    return Response::no_content();
}

// ══════════════════════════════════════════════════════════════════════════
// Tag sub-resource
// ══════════════════════════════════════════════════════════════════════════

Response ProfilesController::listTag(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    return Response::json(200, "OK", registry_json(_ctx.profile(key).tags()).to_string());
}

Response ProfilesController::addTag(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    EquipmentTag tag;
    try {
        tag.from_json(body);
    } catch (const std::exception&) {
        throw WebHttpError(400, "INVALID_BODY", "invalid tag entry body");
    }
    if (tag.id.empty())
        throw WebHttpError(400, "INVALID_BODY", "tag entry requires a non-empty id");
    if (!_ctx.add_tag_to(key, tag))
        throw WebHttpError(409, "DUPLICATE_ENTRY", "tag exists or violates validation: " + tag.id.str());
    return Response::created("/api/profiles/" + key + "/tags/" + tag.id.str(), ok_json().to_string());
}

Response ProfilesController::readTag(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    const auto& reg = _ctx.profile(key).tags();
    auto it = reg.find(path_nsid(pp.get("name")));
    if (it == reg.end())
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "tag not found: " + pp.get("name"));
    return Response::json(200, "OK", it->to_json().to_string());
}

Response ProfilesController::updateTag(const HttpRequest&, const PathParams& pp, const Json& body) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    EquipmentTag tag;
    try {
        tag.from_json(body);
    } catch (const std::exception&) {
        throw WebHttpError(400, "INVALID_BODY", "invalid tag entry body");
    }
    if (tag.id.empty())
        throw WebHttpError(400, "INVALID_BODY", "tag entry requires a non-empty id");
    auto pid = path_nsid(pp.get("name"));   // path_nsid throws 404 on an invalid NSID
    if (pid != tag.id)
        throw WebHttpError(400, "INVALID_FIELD", "path name must match body id");
    if (!_ctx.update_tag_to(key, tag))
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "tag not found: " + tag.id.str());
    return Response::json(200, "OK", ok_json().to_string());
}

Response ProfilesController::removeTag(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    if (!_ctx.remove_tag_from(key, path_nsid(pp.get("name"))))
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "tag not found: " + pp.get("name"));
    return Response::no_content();
}

// ══════════════════════════════════════════════════════════════════════════
// Enchantables sub-resource
// ══════════════════════════════════════════════════════════════════════════

Response ProfilesController::listEnchantables(const HttpRequest&, const PathParams& pp) {
    std::lock_guard<std::mutex> lock(_gate);
    const std::string key = pp.get("key");
    require_profile(_ctx, key);
    const Profile& prof = _ctx.effective_profile(key);
    const NSID item = path_nsid(pp.get("item"));

    // An enchanted book can hold every enchantment — return the full registry
    // (mirrors solve's book exception in CompactAdapter::apply).
    if (item == NSID("minecraft:enchanted_book"))
        return Response::json(200, "OK", registry_json(prof.ench()).to_string());

    if (prof.eq().find(item) == prof.eq().end())
        throw WebHttpError(404, "ENTRY_NOT_FOUND", "equipment not found: " + pp.get("item"));

    const TagResolver* resolver = prof.tag_resolver();
    if (!resolver)
        throw WebHttpError(500, "INTERNAL_ERROR", "profile has no tag resolver attached");

    // Applicability mirrors solve: solve is always Java (WebSolveService pins
    // forge_config.platform = MCE::Java), so the platform gate runs against
    // MCE::Java.  Tag membership comes from the effective view's resolver
    // (nested tags expanded via BFS).
    const auto item_tags = resolver->tags_of(item.str());
    std::vector<EnchInfo> hits;
    for (const auto& e : prof.ench())
        if (CompactAdapter::is_applicable(e, item, item_tags, MCE::Java))
            hits.push_back(e);

    // Deterministic ordering by id (same comparator as registry_json).
    std::sort(hits.begin(), hits.end(),
              [](const EnchInfo& a, const EnchInfo& b) { return a.id.str() < b.id.str(); });
    Json arr = Json::array();
    for (const auto& e : hits)
        arr.push_back(e.to_json());
    return Response::json(200, "OK", arr.to_string());
}

} // namespace web
