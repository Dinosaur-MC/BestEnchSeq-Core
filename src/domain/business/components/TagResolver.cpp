#include "TagResolver.h"
#include "common/io/json.h"

// ---------------------------------------------------------------------------
// Internal: parse a JSON tag file's "values" array into TagValue entries
// ---------------------------------------------------------------------------
void TagResolver::parse_tag_values(const Json &json, std::vector<TagValue> &out) const {
    auto root_var = json.get_value();
    if (!std::holds_alternative<Json::Object>(root_var)) return;

    const auto &root_obj = std::get<Json::Object>(root_var);
    auto values_it       = root_obj.find("values");
    if (values_it == root_obj.end()) return;

    auto values_var = values_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(values_var)) return;

    const auto &values_arr = std::get<Json::Array>(values_var);
    for (const auto &elem : values_arr) {
        auto elem_var = elem.get_value();

        // ---- plain string entry ----
        if (auto *s = std::get_if<Json::String>(&elem_var)) {
            if (!s->empty() && (*s)[0] == '#') {
                out.push_back(TagRef{s->substr(1)});
            } else {
                out.push_back(EntryRef{*s, true});
            }
            continue;
        }

        // ---- object entry  { "id": "...", "required": ... } ----
        if (auto *obj = std::get_if<Json::Object>(&elem_var)) {
            auto id_it = obj->find("id");
            if (id_it == obj->end()) continue;
            auto id_val       = id_it->second.get_value();
            auto *id_str      = std::get_if<Json::String>(&id_val);
            if (!id_str) continue;

            bool required = true;
            auto req_it = obj->find("required");
            if (req_it != obj->end()) {
                auto req_val = req_it->second.get_value();
                if (auto *req_bool = std::get_if<Json::Bool>(&req_val)) {
                    required = *req_bool;
                }
            }

            if (!id_str->empty() && (*id_str)[0] == '#') {
                out.push_back(TagRef{id_str->substr(1)});
            } else {
                out.push_back(EntryRef{*id_str, required});
            }
        }
    }
}

// ---------------------------------------------------------------------------
// load_tag_json  --  load a single tag from a pre-parsed Json DOM
//
// Honors the MC datapack "replace" flag: a tag file with
//   { "replace": true, "values": [...] }
// REPLACES any existing tag with the same key. Without the flag (or with
// "replace": false) the new values MERGE (append) with the existing ones.
// ---------------------------------------------------------------------------
void TagResolver::load_tag_json(const std::string &key, const Json &json) {
    _resolved_cache.clear();

    bool replace = false;
    auto root_var = json.get_value();
    if (auto *root_obj = std::get_if<Json::Object>(&root_var)) {
        auto replace_it = root_obj->find("replace");
        if (replace_it != root_obj->end()) {
            auto replace_val = replace_it->second.get_value();
            if (auto *b = std::get_if<Json::Bool>(&replace_val)) {
                replace = *b;
            }
        }
    }

    auto &vec = _raw_tags[key];
    if (replace)
        vec.clear();
    parse_tag_values(json, vec);
}

// ---------------------------------------------------------------------------
// load_tag_content  --  load a single tag from a raw JSON string
// ---------------------------------------------------------------------------
void TagResolver::load_tag_content(const std::string &key, const std::string &json_content) {
    Json json = Json::parse(json_content);
    load_tag_json(key, json);
}

// ---------------------------------------------------------------------------
// resolve  (single reference)  --  lazy BFS expansion
// ---------------------------------------------------------------------------
std::unordered_set<std::string> TagResolver::resolve(const std::string &reference) const {
    if (reference.empty()) {
        return {};
    }

    // Concrete ID -- return as-is
    if (reference[0] != '#') {
        return {reference};
    }

    std::string start_key = reference.substr(1);

    // Check cache first
    {
        auto cache_it = _resolved_cache.find(start_key);
        if (cache_it != _resolved_cache.end()) {
            return cache_it->second;
        }
    }

    // Tag not defined at all (don't cache the miss so that get_tag can return nullptr)
    if (_raw_tags.find(start_key) == _raw_tags.end()) {
        return {};
    }

    // BFS using vector as queue with head index  (avoids std::queue overhead)
    std::unordered_set<std::string> result;
    std::unordered_set<std::string> visited;
    std::vector<std::string> queue = {start_key};
    visited.insert(start_key);

    size_t head = 0;
    while (head < queue.size()) {
        const auto &current = queue[head++];
        auto it             = _raw_tags.find(current);
        if (it == _raw_tags.end()) {
            continue;
        }
        for (const auto &value : it->second) {
            if (auto *entry = std::get_if<EntryRef>(&value)) {
                result.insert(entry->id);
            } else if (auto *tag = std::get_if<TagRef>(&value)) {
                if (visited.insert(tag->key).second) {
                    queue.push_back(tag->key);
                }
            }
        }
    }

    _resolved_cache[start_key] = result;
    return result;
}

// ---------------------------------------------------------------------------
// resolve  (multiple references)  --  union of individual results
// ---------------------------------------------------------------------------
std::unordered_set<std::string> TagResolver::resolve(
    const std::vector<std::string> &references
) const {
    std::unordered_set<std::string> result;
    for (const auto &ref : references) {
        auto expanded = resolve(ref);
        result.insert(expanded.begin(), expanded.end());
    }
    return result;
}

// ---------------------------------------------------------------------------
// tags_of  --  reverse lookup: concrete ID -> set of `#`-prefixed tag NSIDs
//
// Traverses nested tags: real MC tags nest (e.g. `enchantable/sharp_weapon`
// → `enchantable/melee_weapon` → `swords` → diamond_sword), so an item's tag
// membership is the set of ALL tags whose resolved member set contains it —
// not just the tags that list it as a direct entry.  Each tag is resolved
// once (cached), then membership is checked against the resolved set.
// ---------------------------------------------------------------------------
std::unordered_set<NSID> TagResolver::tags_of(const std::string &concrete_id) const {
    std::unordered_set<NSID> result;
    for (const auto &entry : _raw_tags) {
        const auto &key = entry.first;
        auto resolved = resolve("#" + key);
        if (resolved.count(concrete_id))
            result.insert(NSID("#" + key));
    }
    return result;
}

// ---------------------------------------------------------------------------
// get_tag  --  on-the-fly resolution, returns nullptr for undefined tags
// ---------------------------------------------------------------------------
const std::unordered_set<std::string> *TagResolver::get_tag(
    const std::string &ns, const std::string &name
) const {
    std::string key = ns + ":" + name;

    // Resolve on-the-fly if not already cached.
    // resolve() only caches when the tag key exists in _raw_tags, so a
    // subsequent cache miss means the tag is genuinely undefined => nullptr.
    resolve("#" + key);

    auto it = _resolved_cache.find(key);
    return (it != _resolved_cache.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// raw_values  --  read-only raw tag values (SQL FK reverse-reference checks)
// ---------------------------------------------------------------------------
const std::vector<TagValue> *TagResolver::raw_values(const std::string &key) const {
    auto it = _raw_tags.find(key);
    return (it != _raw_tags.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// add_tag  --  backward-compatible overload
// ---------------------------------------------------------------------------
void TagResolver::add_tag(
    const std::string &key, const std::unordered_set<std::string> &values
) {
    auto &vec = _raw_tags[key];
    vec.clear();
    for (const auto &v : values) {
        if (!v.empty() && v[0] == '#') {
            vec.push_back(TagRef{v.substr(1)});
        } else {
            vec.push_back(EntryRef{v, true});
        }
    }
    // Invalidate any previously-cached expansion for this key
    _resolved_cache.erase(key);
}

// ---------------------------------------------------------------------------
// is_tag  (static)
// ---------------------------------------------------------------------------
bool TagResolver::is_tag(const std::string &reference) {
    return !reference.empty() && reference[0] == '#';
}
