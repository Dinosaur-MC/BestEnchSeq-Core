#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "common/io/json.h"
#include "common/utils/ParserUtils.hpp"

// ---------------------------------------------------------------------------
// Value types for raw (unexpanded) tag entries
// ---------------------------------------------------------------------------

/// Fully qualified concrete ID, e.g. "minecraft:sharpness"
struct EntryRef {
    std::string id;
    bool required = true;
};

/// Tag key reference, e.g. "minecraft:enchantment/treasure" (no '#' prefix)
struct TagRef {
    std::string key;
};

/// A single entry in a tag values array
using TagValue = std::variant<EntryRef, TagRef>;

class TagResolver {
  public:
    // Load all tag files from a data pack directory.
    // Scans data/<ns>/tags/enchantment/ and data/<ns>/tags/item/
    // Stores raw TagValue references -- expansion is deferred to resolve().
    void load_from(const std::filesystem::path &data_pack_dir);

    // Resolve a reference to concrete IDs.
    // If reference starts with '#', it is a tag that gets expanded via BFS.
    // Otherwise, the reference is returned as-is (it is already a concrete ID).
    std::unordered_set<std::string> resolve(const std::string &reference) const;
    std::unordered_set<std::string> resolve(const std::vector<std::string> &references) const;

    // Direct tag access. Returns nullptr if the tag does not exist.
    // Resolves on-the-fly; threads through the mutable cache.
    const std::unordered_set<std::string> *get_tag(const std::string &ns, const std::string &name) const;

    // Programmatically add a raw tag (backward-compatible overload).
    // Each value is stored as EntryRef (no '#') or TagRef (starts with '#').
    void add_tag(const std::string &key, const std::unordered_set<std::string> &values);

    // Check if a reference looks like a tag (starts with '#').
    static bool is_tag(const std::string &reference);

  private:
    std::unordered_map<std::string, std::vector<TagValue>> _raw_tags;
    mutable std::unordered_map<std::string, std::unordered_set<std::string>> _resolved_cache;
};

// ---------------------------------------------------------------------------
// load_from  --  collect raw TagValue, do NOT expand
// ---------------------------------------------------------------------------
inline void TagResolver::load_from(const std::filesystem::path &data_pack_dir) {
    // NOTE: _raw_tags is NOT cleared before loading. Tags from inline JSON and
    // from the filesystem are expected to merge. This allows multiple calls
    // to load_from to accumulate tags without invalidating previously loaded
    // inline tag definitions.
    //
    // The resolved cache IS cleared since any previous expansions may now be
    // stale after new raw tag entries are added.
    _resolved_cache.clear();

    std::filesystem::path data_dir = data_pack_dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return;
    }

    for (const auto &ns_entry : std::filesystem::directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!ns_entry.is_directory()) {
            continue;
        }

        std::string ns = ns_entry.path().filename().string();

        std::filesystem::path tags_dir = ns_entry.path() / "tags";
        if (!std::filesystem::is_directory(tags_dir)) {
            continue;
        }

        for (const auto &category_entry : std::filesystem::directory_iterator(
                 tags_dir, std::filesystem::directory_options::skip_permission_denied)) {
            if (!category_entry.is_directory()) {
                continue;
            }

            // category_entry is data/<ns>/tags/enchantment/ or data/<ns>/tags/item/
            // Recursively find all .json tag files under this category
            try {
                for (const auto &file_entry :
                     std::filesystem::recursive_directory_iterator(
                         category_entry.path(),
                         std::filesystem::directory_options::skip_permission_denied)) {
                    if (!file_entry.is_regular_file()) {
                        continue;
                    }
                    if (file_entry.path().extension() != ".json") {
                        continue;
                    }

                    // Compute tag key: "<ns>:<relative_path_without_extension>"
                    // relative_path is relative to the category directory
                    std::string relative = std::filesystem::relative(
                                               file_entry.path(), category_entry.path()
                    )
                                               .string();

                    // Strip .json extension
                    if (relative.size() >= 5 &&
                        relative.compare(relative.size() - 5, 5, ".json") == 0) {
                        relative = relative.substr(0, relative.size() - 5);
                    }

                    // Normalise path separators to forward slashes
                    for (auto &c : relative) {
                        if (c == '\\') {
                            c = '/';
                        }
                    }

                    std::string key = ns + ":" + relative;

                    // Parse JSON and extract "values" array
                    try {
                        std::string content = ParserUtils::read_file(file_entry.path());
                        Json json          = Json::parse(content);

                        auto root_var = json.get_value();
                        if (!std::holds_alternative<Json::Object>(root_var)) {
                            continue;
                        }

                        const auto &root_obj = std::get<Json::Object>(root_var);
                        auto values_it       = root_obj.find("values");
                        if (values_it == root_obj.end()) {
                            continue;
                        }

                        auto values_var = values_it->second.get_value();
                        if (!std::holds_alternative<Json::Array>(values_var)) {
                            continue;
                        }

                        auto &vec = _raw_tags[key];
                        vec.clear();

                        const auto &values_arr = std::get<Json::Array>(values_var);
                        for (const auto &elem : values_arr) {
                            auto elem_var = elem.get_value();

                            // ---- plain string entry ----
                            if (auto *s = std::get_if<Json::String>(&elem_var)) {
                                if (!s->empty() && (*s)[0] == '#') {
                                    vec.push_back(TagRef{s->substr(1)});
                                } else {
                                    vec.push_back(EntryRef{*s, true});
                                }
                                continue;
                            }

                            // ---- object entry  { "id": "...", "required": ... } ----
                            if (auto *obj = std::get_if<Json::Object>(&elem_var)) {
                                auto id_it = obj->find("id");
                                if (id_it == obj->end()) {
                                    continue;
                                }
                                auto id_val = id_it->second.get_value();
                                auto *id_str = std::get_if<Json::String>(&id_val);
                                if (!id_str) {
                                    continue;
                                }

                                bool required = true;
                                auto req_it = obj->find("required");
                                if (req_it != obj->end()) {
                                    auto req_val = req_it->second.get_value();
                                    if (auto *req_bool = std::get_if<Json::Bool>(&req_val)) {
                                        required = *req_bool;
                                    }
                                }

                                if (!id_str->empty() && (*id_str)[0] == '#') {
                                    vec.push_back(TagRef{id_str->substr(1)});
                                } else {
                                    vec.push_back(EntryRef{*id_str, required});
                                }
                            }
                        }
                    } catch (const std::exception &) {
                        // Skip files that cannot be read or parsed
                        continue;
                    }
                }
            } catch (const std::filesystem::filesystem_error &) {
                // Skip directories that cannot be enumerated
                continue;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// resolve  (single reference)  --  lazy BFS expansion
// ---------------------------------------------------------------------------
inline std::unordered_set<std::string> TagResolver::resolve(const std::string &reference) const {
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
inline std::unordered_set<std::string> TagResolver::resolve(
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
// get_tag  --  on-the-fly resolution, returns nullptr for undefined tags
// ---------------------------------------------------------------------------
inline const std::unordered_set<std::string> *TagResolver::get_tag(
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
// add_tag  --  backward-compatible overload
// ---------------------------------------------------------------------------
inline void TagResolver::add_tag(
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
inline bool TagResolver::is_tag(const std::string &reference) {
    return !reference.empty() && reference[0] == '#';
}
