// MSVC deprecates POSIX names like strdup; suppress the warning since
// we target a cross-platform API and must use the POSIX names for C ABI compat.
#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#include "besq/besq_abi.h"
#include "besq/besq.h"
#include "api/SolvePipeline.h"
#include "io/json.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/EnchInfo.h"
#include "types/EnchSet.h"
#include "types/Equipment.h"
#include "types/ItemStack.h"
#include "types/Platform.h"
#include "utils/ParserUtils.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ====================================================================
// Internal wrapper: the C opaque pointer points to this struct.
// We keep the C++ BesqContext by value so lifetime is trivial.
// ====================================================================
struct BesqContextC {
    BesqContext impl;
    std::string last_error;
};

// ====================================================================
// Helpers
// ====================================================================

/// Execute `expr`; on success return 0, on std::exception store
/// the message in ctx->last_error and return -1.
#define BESQ_CAPI_TRY(ctx, expr)                                            \
    do {                                                                    \
        (ctx)->last_error.clear();                                          \
        try { expr; return 0; }                                             \
        catch (const std::exception& e) {                                   \
            (ctx)->last_error = e.what();                                   \
            return -1;                                                      \
        }                                                                   \
    } while (0)

/// Read a numeric JSON field that is known to exist at obj[key].
/// Assumes the value holds a Json::Number containing int32_t.
static int32_t int_field(const Json::Object& obj, const std::string& key) {
    auto it = obj.find(key);
    if (it == obj.end()) return 0;
    auto val = it->second.get_value();
    if (!std::holds_alternative<Json::Number>(val)) return 0;
    const auto& num = std::get<Json::Number>(val);
    if (std::holds_alternative<int32_t>(num)) return std::get<int32_t>(num);
    if (std::holds_alternative<int64_t>(num))
        return static_cast<int32_t>(std::get<int64_t>(num));
    return 0;
}

/// Build an EnchInfo from a JSON object produced by besq_add_enchantment.
static EnchInfo parse_ench_info_json(const Json::Object& obj) {
    EnchInfo info;

    std::string v;
    v = ParserUtils::get_json_string(obj, "id");
    if (!v.empty()) info.name_id = std::move(v);

    v = ParserUtils::get_json_string(obj, "name");
    if (!v.empty()) info.name = std::move(v);

    if (info.name.empty())
        info.name = info.name_id;

    info.max_level     = ParserUtils::get_json_int(obj, "max_level");
    info.multiplier    = ParserUtils::get_json_int(obj, "multiplier");
    info.limited_level = ParserUtils::get_json_int(obj, "limited_level");
    info.is_treasure   = ParserUtils::get_json_bool(obj, "is_treasure");

    // exclusive_set: array of strings
    {
        auto raw = ParserUtils::get_json_string_array(obj, "exclusive_set");
        for (auto& s : raw)
            info.exclusive_set.insert(std::move(s));
    }

    // applicable_category_ids: optional array of integers
    {
        auto it = obj.find("applicable_category_ids");
        if (it != obj.end()) {
            auto val = it->second.get_value();
            if (std::holds_alternative<Json::Array>(val)) {
                const auto& arr = std::get<Json::Array>(val);
                for (const auto& elem : arr) {
                    auto ev = elem.get_value();
                    if (std::holds_alternative<Json::Number>(ev)) {
                        const auto& num = std::get<Json::Number>(ev);
                        if (std::holds_alternative<int32_t>(num))
                            info.applicable_category_ids.insert(std::get<int32_t>(num));
                        else if (std::holds_alternative<int64_t>(num))
                            info.applicable_category_ids.insert(
                                static_cast<int32_t>(std::get<int64_t>(num)));
                    }
                }
            }
        }
    }

    return info;
}

/// Apply optional patch fields from a JSON object onto an EnchInfo.
/// Used by besq_modify_enchantment.
static void apply_ench_patch(const Json::Object& obj, EnchInfo& patch) {
    if (obj.find("max_level") != obj.end())
        patch.max_level = int_field(obj, "max_level");

    if (obj.find("limited_level") != obj.end())
        patch.limited_level = int_field(obj, "limited_level");

    if (obj.find("multiplier") != obj.end())
        patch.multiplier = int_field(obj, "multiplier");

    if (obj.find("is_treasure") != obj.end()) {
        auto val = obj.at("is_treasure").get_value();
        if (std::holds_alternative<Json::Bool>(val))
            patch.is_treasure = std::get<Json::Bool>(val);
    }

    if (obj.find("name") != obj.end()) {
        auto val = obj.at("name").get_value();
        if (std::holds_alternative<Json::String>(val))
            patch.name = std::get<Json::String>(val);
    }
}

/// Build an Equipment from a JSON object produced by besq_add_equipment.
static Equipment parse_equipment_json(const Json::Object& obj) {
    Equipment eq;

    std::string v;
    v = ParserUtils::get_json_string(obj, "id");
    if (!v.empty()) eq.name_id = std::move(v);

    v = ParserUtils::get_json_string(obj, "name");
    if (!v.empty()) eq.name = std::move(v);

    if (eq.name.empty())
        eq.name = eq.name_id;

    eq.category_id     = ParserUtils::get_json_int(obj, "category_id");
    eq.max_durability  = ParserUtils::get_json_int(obj, "max_durability");

    return eq;
}

/// Resolve a JSON array of enchantment objects into an EnchSet using
/// the given registry.  Each element must have "id" and "level".
static EnchSet parse_ench_set(const Json::Array& arr,
                              const EnchantmentRegistry& ench_reg)
{
    EnchSet result;
    for (const auto& elem : arr) {
        auto eo = std::get<Json::Object>(elem.get_value());
        std::string eid = ParserUtils::get_json_string(eo, "id");
        int32_t lvl     = ParserUtils::get_json_int(eo, "level");
        if (lvl < 1) lvl = 1;

        int32_t idx = ench_reg.get_id(eid);
        if (idx >= 0)
            result.emplace(idx, lvl);
    }
    return result;
}

// ====================================================================
// C ABI implementation
// ====================================================================

extern "C" {

// ── Context lifecycle ──────────────────────────────────────────────────────

BesqContext* besq_create(void) {
    try {
        auto* ctx = new BesqContextC();
        return reinterpret_cast<BesqContext*>(ctx);
    } catch (...) {
        return nullptr;
    }
}

void besq_destroy(BesqContext* ctx) {
    delete reinterpret_cast<BesqContextC*>(ctx);
}

// ── Data loading ────────────────────────────────────────────────────────────

int besq_load_builtin(BesqContext* ctx) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.load_builtin());
}

int besq_load_file(BesqContext* ctx, const char* path) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.load_file(path));
}

int besq_load_data(BesqContext* ctx, const char* path) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        std::vector<std::string> filters = {path};
        c->impl.load_data(filters);
    );
}

// ── Profile management ──────────────────────────────────────────────────────

const char* besq_active_profile(BesqContext* ctx) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    try {
        return c->impl.active_profile().c_str();
    } catch (...) {
        c->last_error = "No active profile";
        return nullptr;
    }
}

char** besq_list_profiles(BesqContext* ctx, int* out_count) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    try {
        auto names = c->impl.list_profiles();
        *out_count = static_cast<int>(names.size());
        char** arr = static_cast<char**>(std::malloc(names.size() * sizeof(char*)));
        if (!arr) {
            *out_count = 0;
            return nullptr;
        }
        for (size_t i = 0; i < names.size(); ++i) {
            arr[i] = strdup(names[i].c_str());
            if (!arr[i]) {
                for (size_t j = 0; j < i; ++j) std::free(arr[j]);
                std::free(arr);
                *out_count = 0;
                return nullptr;
            }
        }
        return arr;
    } catch (const std::exception& e) {
        c->last_error = e.what();
        *out_count = 0;
        return nullptr;
    }
}

void besq_free_string_list(char** list, int count) {
    if (!list) return;
    for (int i = 0; i < count; ++i)
        std::free(list[i]);
    std::free(list);
}

int besq_activate_profile(BesqContext* ctx, const char* name) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.activate_profile(name));
}

int besq_fork_profile(BesqContext* ctx, const char* source, const char* dest) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.fork_profile(source, dest));
}

int besq_merge_profile(BesqContext* ctx, const char* source, const char* dest) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.merge_profile(source, dest));
}

int besq_remove_profile(BesqContext* ctx, const char* name) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.remove_profile(name));
}

// ── Registry editing ────────────────────────────────────────────────────────

int besq_add_enchantment(BesqContext* ctx, const char* json_info) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        auto json = Json::parse(json_info);
        auto obj  = std::get<Json::Object>(json.get_value());
        auto info = parse_ench_info_json(obj);
        if (!c->impl.add_enchantment(info))
            throw std::runtime_error("add_enchantment failed (duplicate name_id?)");
    );
}

int besq_remove_enchantment(BesqContext* ctx, const char* name_id) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        if (!c->impl.remove_enchantment(name_id))
            throw std::runtime_error(std::string("enchantment not found: ") + name_id);
    );
}

int besq_modify_enchantment(BesqContext* ctx, const char* json_patch) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        auto json = Json::parse(json_patch);
        auto obj  = std::get<Json::Object>(json.get_value());

        auto id_it = obj.find("id");
        if (id_it == obj.end())
            throw std::runtime_error("Missing 'id' field in enchantment patch");

        std::string ench_id = std::get<Json::String>(id_it->second.get_value());
        EnchInfo patch;
        apply_ench_patch(obj, patch);

        if (!c->impl.modify_enchantment(ench_id, patch))
            throw std::runtime_error(std::string("enchantment not found: ") + ench_id);
    );
}

int besq_add_equipment(BesqContext* ctx, const char* json_eq) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        auto json = Json::parse(json_eq);
        auto obj  = std::get<Json::Object>(json.get_value());
        auto eq   = parse_equipment_json(obj);
        if (!c->impl.add_equipment(eq))
            throw std::runtime_error("add_equipment failed (duplicate name_id?)");
    );
}

int besq_remove_equipment(BesqContext* ctx, const char* name_id) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        if (!c->impl.remove_equipment(name_id))
            throw std::runtime_error(std::string("equipment not found: ") + name_id);
    );
}

int besq_add_category(BesqContext* ctx, const char* name) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c, c->impl.add_category(name));
}

// ── Solve ───────────────────────────────────────────────────────────────────

/// Expected JSON input:
/// {
///   "target": {
///     "equipment": "diamond_sword",
///     "enchantments": [ {"id":"sharpness","level":5}, ... ]
///   },
///   "source": [ {"id":"sharpness","level":3}, ... ],
///   "algorithm": "greedy",
///   "platform": "java",
///   "max_solutions": 1,
///   "mode": "direct"
/// }
char* besq_solve(BesqContext* ctx, const char* json_input) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    c->last_error.clear();

    try {
        auto json = Json::parse(json_input);
        auto root = std::get<Json::Object>(json.get_value());
        SolveInput input;

        // ── Target ────────────────────────────────────────────────────────
        auto target_it = root.find("target");
        if (target_it != root.end()) {
            auto target_obj =
                std::get<Json::Object>(target_it->second.get_value());

            std::string eq_id = ParserUtils::get_json_string(target_obj, "equipment");
            if (!eq_id.empty()) {
                const auto& eq_reg = c->impl.equipment();
                int32_t eq_idx = eq_reg.get_id(eq_id);
                if (eq_idx >= 0) {
                    input.target_item =
                        ItemStack(eq_reg.get(eq_idx), EnchSet{}, 0);
                }
            }

            // Target enchantments (desired final state)
            auto ench_it = target_obj.find("enchantments");
            if (ench_it != target_obj.end()) {
                auto ench_arr =
                    std::get<Json::Array>(ench_it->second.get_value());
                input.target_item.enchantments =
                    parse_ench_set(ench_arr, c->impl.enchantments());
            }
        }

        // ── Source enchantments ───────────────────────────────────────────
        auto src_it = root.find("source");
        if (src_it != root.end()) {
            auto src_arr = std::get<Json::Array>(src_it->second.get_value());
            input.source_enchantments =
                parse_ench_set(src_arr, c->impl.enchantments());
        }

        // ── Algorithm ─────────────────────────────────────────────────────
        input.algorithm = ParserUtils::get_json_string(root, "algorithm");
        if (input.algorithm.empty())
            input.algorithm = "greedy";

        // ── Platform ──────────────────────────────────────────────────────
        std::string plat = ParserUtils::get_json_string(root, "platform");
        if (plat == "java")
            input.forge_config.platform = MCE::Java;
        else if (plat == "bedrock")
            input.forge_config.platform = MCE::Bedrock;
        else
            input.forge_config.platform = MCE::Java;

        // ── Mode ──────────────────────────────────────────────────────────
        std::string mode = ParserUtils::get_json_string(root, "mode");
        input.is_inventory_mode = (mode == "inventory");

        // ── Max solutions ─────────────────────────────────────────────────
        int32_t max_sol = ParserUtils::get_json_int(root, "max_solutions");
        if (max_sol > 0)
            input.search_config.max_solutions = max_sol;

        // ── Solve ─────────────────────────────────────────────────────────
        auto result = c->impl.solve(input);
        auto json_out = result.to_json(c->impl.enchantments(),
                                       c->impl.categories());
        return strdup(json_out.c_str());
    }
    catch (const std::exception& e) {
        c->last_error = e.what();
        return nullptr;
    }
}

void besq_free_string(char* str) {
    std::free(str);
}

// ── Persistence ────────────────────────────────────────────────────────────

int besq_export_registry(BesqContext* ctx, const char* path) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    BESQ_CAPI_TRY(c,
        if (!c->impl.export_registry(path))
            throw std::runtime_error(std::string("export failed: ") + path);
    );
}

// ── Error handling ──────────────────────────────────────────────────────────

const char* besq_last_error(BesqContext* ctx) {
    auto* c = reinterpret_cast<BesqContextC*>(ctx);
    return c->last_error.c_str();
}

} // extern "C"
