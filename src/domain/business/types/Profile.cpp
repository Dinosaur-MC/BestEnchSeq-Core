#include "Profile.h"
#include "domain/business/components/Serializer.h"  // for EnchantmentRegistry/EquipmentRegistry << >>
#include "common/CommonTypes.h"

#include <chrono>
#include <stdexcept>

// ============================================================================
// Construction
// ============================================================================

Profile::Profile(NSID name) : _meta(ProfileMetadata(std::move(name))) {}

Profile::Profile(ProfileMetadata meta, EnchantmentRegistry ench, EquipmentRegistry eq,
                 TagRegistry tags)
    : _meta(std::move(meta))
    , _ench(std::move(ench))
    , _eq(std::move(eq))
    , _tags(std::move(tags))
{
    // ProfileMetadata constructors already set timestamps to now.
    // Only guard against edge cases like raw default-constructed metadata.
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
        for (const auto& cat_nsid : info.supported_items) {
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
    Json obj = Json::object()
        .set(std::string(ProfileMetadata::KEY_NAME),        Json(_meta.name.str()))
        .set(std::string(ProfileMetadata::KEY_DESCRIPTION), Json(_meta.description))
        .set(std::string(ProfileMetadata::KEY_AUTHOR),      Json(_meta.author))
        .set(std::string(ProfileMetadata::KEY_VERSION),     Json(_meta.version));

    // Enchantments
    Json ej;
    ej << _ench;
    obj.set(std::string(ProfileMetadata::KEY_ENCHANTMENTS), ej);

    // Equipment
    Json eqj;
    eqj << _eq;
    obj.set(std::string(ProfileMetadata::KEY_EQUIPMENTS), eqj);

    // Tags — serialize as array of name strings (vanilla.json format)
    {
        Json::Array arr;
        for (const auto& [id, tag] : _tags.data())
            arr.push_back(Json(tag.name));
        obj.set(std::string(ProfileMetadata::KEY_TAGS), Json(std::move(arr)));
    }

    return obj;
}

void Profile::from_json(const Json& json) {
    *this = from_json_static(json);
}

Profile Profile::from_json_static(const Json& json) {
    Profile p;

    if (json.type() != JsonType::Object)
        return p;

    p._meta.name        = NSID(json.has(std::string(ProfileMetadata::KEY_NAME)) ? json[std::string(ProfileMetadata::KEY_NAME)].as<std::string>() : "");
    p._meta.description = json.has(std::string(ProfileMetadata::KEY_DESCRIPTION)) ? json[std::string(ProfileMetadata::KEY_DESCRIPTION)].as<std::string>() : "";
    p._meta.author      = json.has(std::string(ProfileMetadata::KEY_AUTHOR)) ? json[std::string(ProfileMetadata::KEY_AUTHOR)].as<std::string>() : "";
    p._meta.version     = json.has(std::string(ProfileMetadata::KEY_VERSION)) ? json[std::string(ProfileMetadata::KEY_VERSION)].as<std::string>() : "";
    p._meta.created_at  = std::chrono::system_clock::now();
    p._meta.updated_at  = p._meta.created_at;

    // Enchantments
    if (json.has(std::string(ProfileMetadata::KEY_ENCHANTMENTS))) {
        Json ench_json = json[std::string(ProfileMetadata::KEY_ENCHANTMENTS)];
        ench_json >> p._ench;
    }

    // Equipment
    if (json.has(std::string(ProfileMetadata::KEY_EQUIPMENTS))) {
        Json eq_json = json[std::string(ProfileMetadata::KEY_EQUIPMENTS)];
        eq_json >> p._eq;
    }

    // Tags
    if (json.has(std::string(ProfileMetadata::KEY_TAGS))) {
        Json tag_val = json[std::string(ProfileMetadata::KEY_TAGS)];
        if (tag_val.type() == JsonType::Array) {
            Json::Array arr = tag_val.as<Json::Array>();
            for (const auto& elem : arr) {
                if (elem.type() == JsonType::String) {
                    std::string name = elem.as<std::string>();
                    EquipmentTag tag;
                    tag.id   = NSID("#minecraft:" + name);
                    tag.name = std::move(name);
                    p._tags.insert(std::move(tag));
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
    json = Json::object()
        .set(std::string(ProfileMetadata::KEY_NAME),        Json(meta.name.str()))
        .set(std::string(ProfileMetadata::KEY_DESCRIPTION), Json(meta.description))
        .set(std::string(ProfileMetadata::KEY_AUTHOR),      Json(meta.author))
        .set(std::string(ProfileMetadata::KEY_VERSION),     Json(meta.version));
    return json;
}

const Json& operator>>(const Json& json, ProfileMetadata& meta) {
    if (json.type() != JsonType::Object)
        return json;

    meta.name        = NSID(json.has(std::string(ProfileMetadata::KEY_NAME)) ? json[std::string(ProfileMetadata::KEY_NAME)].as<std::string>() : "");
    meta.description = json.has(std::string(ProfileMetadata::KEY_DESCRIPTION)) ? json[std::string(ProfileMetadata::KEY_DESCRIPTION)].as<std::string>() : "";
    meta.author      = json.has(std::string(ProfileMetadata::KEY_AUTHOR)) ? json[std::string(ProfileMetadata::KEY_AUTHOR)].as<std::string>() : "";
    meta.version     = json.has(std::string(ProfileMetadata::KEY_VERSION)) ? json[std::string(ProfileMetadata::KEY_VERSION)].as<std::string>() : "";
    return json;
}
