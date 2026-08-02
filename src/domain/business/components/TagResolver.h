#pragma once

#include "common/CommonTypes.h"
#include "common/io/json.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

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
    // ── In-memory tag loading (no filesystem access) ──────────────────────

    /// Load a single tag from a Json DOM object expected to have a "values" array.
    /// The key is the fully qualified tag identifier (e.g. "minecraft:enchantment/treasure").
    void load_tag_json(const std::string &key, const Json &json);

    /// Load a single tag from a raw JSON string.
    /// Parses the string as JSON then delegates to load_tag_json().
    void load_tag_content(const std::string &key, const std::string &json_content);

    // ── Resolution ────────────────────────────────────────────────────────

    /// Resolve a reference to concrete IDs.
    /// If reference starts with '#', it is a tag that gets expanded via BFS.
    /// Otherwise, the reference is returned as-is (it is already a concrete ID).
    std::unordered_set<std::string> resolve(const std::string &reference) const;
    std::unordered_set<std::string> resolve(const std::vector<std::string> &references) const;

    /// Return the set of `#`-prefixed tag NSIDs whose raw values contain
    /// `concrete_id`. Used at the business→algorithm boundary to compute an
    /// item's tag membership for applicability.
    std::unordered_set<NSID> tags_of(const std::string &concrete_id) const;

    /// Direct tag access. Returns nullptr if the tag does not exist.
    /// Resolves on-the-fly; threads through the mutable cache.
    const std::unordered_set<std::string> *get_tag(const std::string &ns, const std::string &name) const;

    /// Programmatically add a raw tag (backward-compatible overload).
    /// Each value is stored as EntryRef (no '#') or TagRef (starts with '#').
    void add_tag(const std::string &key, const std::unordered_set<std::string> &values);

    /// Check if a reference looks like a tag (starts with '#').
    static bool is_tag(const std::string &reference);

  private:
    void parse_tag_values(const Json &json, std::vector<TagValue> &out) const;

  private:
    std::unordered_map<std::string, std::vector<TagValue>> _raw_tags;
    mutable std::unordered_map<std::string, std::unordered_set<std::string>> _resolved_cache;
};
