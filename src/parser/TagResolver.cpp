#include "parser/TagResolver.h"
#include "parser/ParserUtils.h"
#include "io/json.h"

#include <utility>
#include <variant>

namespace {

// ---------------------------------------------------------------------------
// Internal: recursively resolve a tag value, detecting cycles
// ---------------------------------------------------------------------------
std::unordered_set<std::string> resolve_raw_value(
    const std::string &value,
    const std::unordered_map<std::string, std::vector<std::string>> &raw_tags,
    std::unordered_set<std::string> &visiting
) {
    if (value.empty()) {
        return {};
    }

    // Concrete value (no '#' prefix) -- return as-is
    if (value[0] != '#') {
        return {value};
    }

    // Tag reference -- look up in raw tag map
    std::string tag_key = value.substr(1);

    // Cycle detection: if we are already resolving this tag, stop
    if (visiting.count(tag_key)) {
        return {};
    }

    auto it = raw_tags.find(tag_key);
    if (it == raw_tags.end()) {
        // Tag not found -- return empty
        return {};
    }

    visiting.insert(tag_key);

    std::unordered_set<std::string> result;
    for (const auto &v : it->second) {
        auto expanded = resolve_raw_value(v, raw_tags, visiting);
        result.insert(expanded.begin(), expanded.end());
    }

    visiting.erase(tag_key);
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// load_from
// ---------------------------------------------------------------------------
void TagResolver::load_from(const std::filesystem::path &data_pack_dir) {
    // NOTE: _tags is NOT cleared before loading. Tags from inline JSON and
    // from the filesystem are expected to merge. This allows multiple calls
    // to load_from to accumulate tags without invalidating previously loaded
    // inline tag definitions.

    std::filesystem::path data_dir = data_pack_dir / "data";
    if (!std::filesystem::is_directory(data_dir)) {
        return;
    }

    // First pass: collect all raw tag values (including '#tag' references)
    std::unordered_map<std::string, std::vector<std::string>> raw_tags;

    for (const auto &ns_entry : std::filesystem::directory_iterator(data_dir)) {
        if (!ns_entry.is_directory()) {
            continue;
        }

        std::string ns = ns_entry.path().filename().string();

        std::filesystem::path tags_dir = ns_entry.path() / "tags";
        if (!std::filesystem::is_directory(tags_dir)) {
            continue;
        }

        for (const auto &category_entry : std::filesystem::directory_iterator(tags_dir)) {
            if (!category_entry.is_directory()) {
                continue;
            }

            // category_entry is data/<ns>/tags/enchantment/ or data/<ns>/tags/item/
            // Recursively find all .json tag files under this category
            try {
                for (const auto &file_entry :
                     std::filesystem::recursive_directory_iterator(category_entry.path())) {
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

                        const auto &values_arr = std::get<Json::Array>(values_var);
                        for (const auto &elem : values_arr) {
                            auto elem_var = elem.get_value();
                            if (std::holds_alternative<Json::String>(elem_var)) {
                                raw_tags[key].push_back(std::get<Json::String>(elem_var));
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

    // Second pass: resolve inter-tag references
    for (const auto &[key, values] : raw_tags) {
        std::unordered_set<std::string> resolved;
        std::unordered_set<std::string> visiting;

        // Insert the key itself to detect self-references
        visiting.insert(key);

        for (const auto &val : values) {
            auto expanded = resolve_raw_value(val, raw_tags, visiting);
            resolved.insert(expanded.begin(), expanded.end());
        }

        visiting.erase(key);
        _tags[key] = std::move(resolved);
    }
}

// ---------------------------------------------------------------------------
// resolve (single reference)
// ---------------------------------------------------------------------------
std::unordered_set<std::string> TagResolver::resolve(const std::string &reference) const {
    if (reference.empty()) {
        return {};
    }

    if (reference[0] == '#') {
        std::string tag_key = reference.substr(1);
        auto it             = _tags.find(tag_key);
        if (it != _tags.end()) {
            return it->second;
        }
        return {};
    }

    return {reference};
}

// ---------------------------------------------------------------------------
// resolve (multiple references) -- union of individual results
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
// get_tag
// ---------------------------------------------------------------------------
const std::unordered_set<std::string> *TagResolver::get_tag(
    const std::string &ns, const std::string &name
) const {
    std::string key = ns + ":" + name;
    auto it         = _tags.find(key);
    if (it != _tags.end()) {
        return &it->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// add_tag
// ---------------------------------------------------------------------------
void TagResolver::add_tag(const std::string &key, const std::unordered_set<std::string> &values) {
    _tags[key] = values;
}

// ---------------------------------------------------------------------------
// is_tag (static)
// ---------------------------------------------------------------------------
bool TagResolver::is_tag(const std::string &reference) {
    return !reference.empty() && reference[0] == '#';
}
