#include "Profile.h"
#include "domain/business/components/Serializer.h"  // for EnchantmentRegistry/EquipmentRegistry << >>
#include "common/CommonTypes.h"

#include <chrono>
#include <stdexcept>

// ============================================================================
// Construction
// ============================================================================

Profile::Profile(NSID name) {
    _meta.name = std::move(name);
    _meta.created_at = std::chrono::system_clock::now();
    _meta.updated_at = _meta.created_at;
}

Profile::Profile(ProfileMetadata meta, EnchantmentRegistry ench, EquipmentRegistry eq,
                 EquipmentTagRegistry tags)
    : _meta(std::move(meta))
    , _ench(std::move(ench))
    , _eq(std::move(eq))
    , _tags(std::move(tags))
{
    if (_meta.created_at == std::chrono::system_clock::time_point{})
        _meta.created_at = std::chrono::system_clock::now();
    if (_meta.updated_at == std::chrono::system_clock::time_point{})
        _meta.updated_at = std::chrono::system_clock::now();
}

// ============================================================================
// Proxy Queries
// ============================================================================

bool Profile::is_compatible(const NSID& a, const NSID& b) const {
    return !_ench.is_incompatible(a, b);
}

std::vector<Equipment> Profile::applicable_equipment(const NSID& ench_id) const {
    std::vector<Equipment> result;
    try {
        const auto& info = _ench.at(ench_id);
        for (const auto& cat_nsid : info.applicable_equipments) {
            auto eq_list = _eq.get_by_category(cat_nsid);
            result.insert(result.end(), eq_list.begin(), eq_list.end());
        }
    } catch (const std::out_of_range&) {
        // Enchantment not found — return empty
    }
    return result;
}

bool Profile::has_enchantment(const NSID& id) const {
    return _ench.contains(id);
}

bool Profile::has_equipment(const NSID& id) const {
    return _eq.contains(id);
}

// ============================================================================
// Proxy Mutations
// ============================================================================

bool Profile::add_enchantment(const EnchInfo& info) {
    auto [it, inserted] = _ench.insert(info);
    if (inserted) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return inserted;
}

bool Profile::update_enchantment(const EnchInfo& info) {
    bool updated = _ench.update(info);
    if (updated) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return updated;
}

bool Profile::remove_enchantment(const NSID& id) {
    bool removed = _ench.erase(id);
    if (removed) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return removed;
}

bool Profile::add_equipment(const Equipment& eq) {
    auto [it, inserted] = _eq.insert(eq);
    if (inserted) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return inserted;
}

bool Profile::remove_equipment(const NSID& id) {
    bool removed = _eq.erase(id);
    if (removed) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return removed;
}

bool Profile::add_tag(const EquipmentTag& tag) {
    auto [it, inserted] = _tags.insert(tag);
    if (inserted) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return inserted;
}

bool Profile::remove_tag(const NSID& id) {
    bool removed = _tags.erase(id);
    if (removed) {
        _meta.updated_at = std::chrono::system_clock::now();
    }
    return removed;
}

// ============================================================================
// Validation
// ============================================================================

bool Profile::validate() const {
    // Verify all enchantment exclusive_set entries reference known enchantments
    for (const auto& [nsid, info] : _ench.data()) {
        for (const auto& excl : info.exclusive_set) {
            if (!_ench.contains(excl)) {
                return false; // references unknown enchantment
            }
        }
    }
    // Verify all equipment categories reference known tags
    for (const auto& [nsid, eq] : _eq.data()) {
        if (!eq.category.empty() && !_tags.contains(eq.category)) {
            return false; // references unknown tag
        }
    }
    return true;
}

// ============================================================================
// Clone
// ============================================================================

Profile Profile::clone(const NSID& new_name) const {
    Profile p;
    p._meta            = _meta;
    p._meta.name       = new_name;
    p._meta.parent     = _meta.name.str();
    p._meta.created_at = std::chrono::system_clock::now();
    p._meta.updated_at = p._meta.created_at;
    p._ench            = _ench;  // value-type deep copy
    p._eq              = _eq;
    p._tags            = _tags;
    return p;
}

// ============================================================================
// Json streaming operators (global scope, ADL via Json)
// ============================================================================

// ============================================================================
// Profile::to_json / from_json
// ============================================================================

Json Profile::to_json() const {
    Json::Object obj;

    // Metadata
    obj[std::string(ProfileMetadata::KEY_NAME)]        = Json(Json::String(_meta.name.str()));
    obj[std::string(ProfileMetadata::KEY_DESCRIPTION)] = Json(Json::String(_meta.description));
    obj[std::string(ProfileMetadata::KEY_AUTHOR)]      = Json(Json::String(_meta.author));
    obj[std::string(ProfileMetadata::KEY_VERSION)]     = Json(Json::String(_meta.version));

    // Enchantments
    Json ej;
    ej << _ench;
    obj[std::string(ProfileMetadata::KEY_ENCHANTMENTS)] = ej;

    // Equipment
    Json eqj;
    eqj << _eq;
    obj[std::string(ProfileMetadata::KEY_EQUIPMENTS)] = eqj;

    // Tags — serialize as array of name strings (vanilla.json format)
    {
        Json::Array arr;
        for (const auto& [id, tag] : _tags.data())
            arr.push_back(Json(Json::String(tag.name)));
        obj[std::string(ProfileMetadata::KEY_TAGS)] = Json(std::move(arr));
    }

    return Json(std::move(obj));
}

void Profile::from_json(const Json& json) {
    *this = from_json_static(json);
}

Profile Profile::from_json_static(const Json& json) {
    Profile p;

    auto obj = [&]() -> Json::Object {
        if (json.type() == JsonType::Object)
            return std::get<Json::Object>(json.get_value());
        return {};
    }();

    auto get_str = [&](const std::string& key) -> std::string {
        auto it = obj.find(key);
        if (it != obj.end() && it->second.type() == JsonType::String)
            return std::get<Json::String>(it->second.get_value());
        return {};
    };

    p._meta.name        = NSID(get_str(std::string(ProfileMetadata::KEY_NAME)));
    p._meta.description = get_str(std::string(ProfileMetadata::KEY_DESCRIPTION));
    p._meta.author      = get_str(std::string(ProfileMetadata::KEY_AUTHOR));
    p._meta.version     = get_str(std::string(ProfileMetadata::KEY_VERSION));
    p._meta.created_at  = std::chrono::system_clock::now();
    p._meta.updated_at  = p._meta.created_at;

    // Enchantments
    {
        auto it = obj.find(std::string(ProfileMetadata::KEY_ENCHANTMENTS));
        if (it != obj.end()) {
            Json ench_json(it->second);
            ench_json >> p._ench;
        }
    }

    // Equipment
    {
        auto it = obj.find(std::string(ProfileMetadata::KEY_EQUIPMENTS));
        if (it != obj.end()) {
            Json eq_json(it->second);
            eq_json >> p._eq;
        }
    }

    // Tags
    {
        auto it = obj.find(std::string(ProfileMetadata::KEY_TAGS));
        if (it != obj.end()) {
            auto tag_val = it->second.get_value();
            if (std::holds_alternative<Json::Array>(tag_val)) {
                const auto& arr = std::get<Json::Array>(tag_val);
                for (const auto& elem : arr) {
                    if (elem.type() == JsonType::String) {
                        std::string name = std::get<Json::String>(elem.get_value());
                        EquipmentTag tag;
                        tag.id   = NSID("#minecraft:" + name);
                        tag.name = std::move(name);
                        p._tags.insert(std::move(tag));
                    }
                }
            }
        }
    }

    return p;
}

// ============================================================================
// Global operators — delegate to member methods
// ============================================================================

Json& operator<<(Json& json, const Profile& profile) {
    json = profile.to_json();
    return json;
}

const Json& operator>>(const Json& json, Profile& profile) {
    profile = Profile::from_json_static(json);
    return json;
}

Json& operator<<(Json& json, const ProfileMetadata& meta) {
    json = Json(Json::Object{
        {std::string(ProfileMetadata::KEY_NAME),        Json(Json::String(meta.name.str()))},
        {std::string(ProfileMetadata::KEY_DESCRIPTION), Json(Json::String(meta.description))},
        {std::string(ProfileMetadata::KEY_AUTHOR),      Json(Json::String(meta.author))},
        {std::string(ProfileMetadata::KEY_VERSION),     Json(Json::String(meta.version))},
    });
    return json;
}

const Json& operator>>(const Json& json, ProfileMetadata& meta) {
    auto obj = [&]() -> Json::Object {
        if (json.type() == JsonType::Object)
            return std::get<Json::Object>(json.get_value());
        return {};
    }();

    auto get_str = [&](const std::string& key) -> std::string {
        auto it = obj.find(key);
        if (it != obj.end() && it->second.type() == JsonType::String)
            return std::get<Json::String>(it->second.get_value());
        return {};
    };

    meta.name        = NSID(get_str(std::string(ProfileMetadata::KEY_NAME)));
    meta.description = get_str(std::string(ProfileMetadata::KEY_DESCRIPTION));
    meta.author      = get_str(std::string(ProfileMetadata::KEY_AUTHOR));
    meta.version     = get_str(std::string(ProfileMetadata::KEY_VERSION));
    return json;
}
