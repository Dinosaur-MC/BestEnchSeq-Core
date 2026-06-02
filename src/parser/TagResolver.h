#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class TagResolver {
  public:
    // Load all tag files from a data pack directory.
    // Scans data/<ns>/tags/enchantment/ and data/<ns>/tags/item/
    void load_from(const std::filesystem::path &data_pack_dir);

    // Resolve a reference to concrete IDs.
    // If reference starts with '#', it is a tag that gets expanded.
    // Otherwise, the reference is returned as-is (it is already a concrete ID).
    std::unordered_set<std::string> resolve(const std::string &reference) const;
    std::unordered_set<std::string> resolve(const std::vector<std::string> &references) const;

    // Direct tag access. Returns nullptr if the tag does not exist.
    const std::unordered_set<std::string> *get_tag(const std::string &ns, const std::string &name) const;

    // Check if a reference looks like a tag (starts with '#').
    static bool is_tag(const std::string &reference);

  private:
    // Map: "namespace:tag_name" -> set of concrete IDs
    std::unordered_map<std::string, std::unordered_set<std::string>> _tags;
};
