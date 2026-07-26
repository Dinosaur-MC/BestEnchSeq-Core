// MSVC deprecates POSIX names like strdup; suppress the warning since
// we target a cross-platform API and must use the POSIX names for C ABI compat.
#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#include "domain/interface/abi/abi.h"
#include "domain/interface/BesqContext.h"
#include "common/io/json.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/Item.h"
#include "common/CommonTypes.h"

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
    return static_cast<int32_t>(it->second.as<int64_t>());
}

/// Build an EnchInfo from a JSON object produced by besq_add_enchantment.
static EnchInfo parse_ench_info_json(const Json::Object& obj) {
    EnchInfo info;

    {
        auto it = obj.find("id");
        if (it != obj.end()) {
            std::string v = it->second.as<std::string>();
            if (!v.empty()) info.id = NSID(std::move(v));
        }
    }

    {
        auto it = obj.find("name");
        if (it != obj.end()) {
            std::string v = it->second.as<std::string>();
            if (!v.empty()) info.name = std::move(v);
        }
    }

    if (info.name.empty())
        info.name = info.id.str();

    {
        auto it = obj.find("max_level");
        if (it != obj.end()) info.max_level = it->second.as<int32_t>();
    }
    {
        auto it = obj.find("multiplier");
        if (it != obj.end()) info.multiplier = it->second.as<int32_t>();
    }
    {
        auto it = obj.find("limited_level");
        if (it != obj.end()) info.limited_level = it->second.as<int32_t>();
    }
    {
        auto it = obj.find("is_treasure");
        if (it != obj.end()) info.is_treasure = it->second.as<bool>();
    }

    // exclusive_set: array of strings (now NSIDs)
    {
        auto it = obj.find("exclusive_set");
        if (it != obj.end()) {
            Json::Array arr = it->second.as<Json::Array>();
            for (const auto& elem : arr) {
                std::string s = elem.as<std::string>();
                info.exclusive_set.insert(NSID(std::move(s)));
            }
        }
    }

    // applicable_equipments: optional array of NSID strings (formerly applicable_category_ids)
    {
        auto it = obj.find("applicable_equipments");
        if (it != obj.end()) {
            Json::Array arr = it->second.as<Json::Array>();
            for (const auto& elem : arr) {
                std::string s = elem.as<std::string>();
                info.applicable_equipments.insert(NSID(std::move(s)));
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
        patch.is_treasure = obj.at("is_treasure").as<bool>();
    }

    if (obj.find("name") != obj.end()) {
        patch.name = obj.at("name").as<std::string>();
    }
}

/// Build an Equipment from a JSON object produced by besq_add_equipment.
static Equipment parse_equipment_json(const Json::Object& obj) {
    Equipment eq;

    {
        auto it = obj.find("id");
        if (it != obj.end()) {
            std::string v = it->second.as<std::string>();
            if (!v.empty()) eq.id = NSID(std::move(v));
        }
    }

    {
        auto it = obj.find("name");
        if (it != obj.end()) {
            std::string v = it->second.as<std::string>();
            if (!v.empty()) eq.name = std::move(v);
        }
    }

    if (eq.name.empty())
        eq.name = eq.id.str();

    {
        auto it = obj.find("category");
        if (it != obj.end()) {
            std::string v = it->second.as<std::string>();
            if (!v.empty())
                eq.category = NSID(std::move(v));
            else
                eq.category = NSID("unknown");
        } else {
            eq.category = NSID("unknown");
        }
    }

    {
        auto it = obj.find("max_durability");
        if (it != obj.end()) eq.max_durability = it->second.as<int32_t>();
    }

    return eq;
}

/// Resolve a JSON array of enchantment objects into an EnchSet using
/// the given registry.  Each element must have "id" and "level".
static EnchSet parse_ench_set(const Json::Array& arr,
                              const EnchantmentRegistry& ench_reg)
{
    EnchSet result;
    for (const auto& elem : arr) {
        auto eo = elem.as<Json::Object>();
        std::string eid;
        int32_t lvl     = 0;
        {
            auto it = eo.find("id");
            if (it != eo.end()) eid = it->second.as<std::string>();
        }
        {
            auto it = eo.find("level");
            if (it != eo.end()) lvl = it->second.as<int32_t>();
        }
        if (lvl < 1) lvl = 1;

        auto ench_it = ench_reg.find(NSID(eid));
        if (ench_it != ench_reg.end()) {
            result.emplace(ench_it->id, ench_it->name, lvl);
        }
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
        auto obj  = json.as<Json::Object>();
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
        auto obj  = json.as<Json::Object>();

        auto id_it = obj.find("id");
        if (id_it == obj.end())
            throw std::runtime_error("Missing 'id' field in enchantment patch");

        std::string ench_id = id_it->second.as<std::string>();
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
        auto obj  = json.as<Json::Object>();
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
        auto root = json.as<Json::Object>();
        SolveRequest request;

        // ── Target ────────────────────────────────────────────────────────
        auto target_it = root.find("target");
        if (target_it != root.end()) {
            auto target_obj = target_it->second.as<Json::Object>();

            std::string eq_id;
            {
                auto it = target_obj.find("equipment");
                if (it != target_obj.end()) eq_id = it->second.as<std::string>();
            }
            if (!eq_id.empty()) {
                const auto& eq_reg = c->impl.equipment();
                auto eq_it = eq_reg.find(NSID(eq_id));
                if (eq_it != eq_reg.end()) {
                    request.target_item =
                        Item(eq_it->id, EnchSet{}, 0, eq_it->max_durability);
                }
            }

            // Target enchantments (desired final state)
            auto ench_it = target_obj.find("enchantments");
            if (ench_it != target_obj.end()) {
                auto ench_arr = ench_it->second.as<Json::Array>();
                request.target_item.enchantments =
                    parse_ench_set(ench_arr, c->impl.enchantments());
            }
        }

        // ── Source enchantments (DirectPayload) ───────────────────────────
        EnchSet source_enchants;
        auto src_it = root.find("source");
        if (src_it != root.end()) {
            auto src_arr = src_it->second.as<Json::Array>();
            source_enchants =
                parse_ench_set(src_arr, c->impl.enchantments());
        }

        // ── Mode & payload ────────────────────────────────────────────────
        std::string mode_str;
        {
            auto it = root.find("mode");
            if (it != root.end()) mode_str = it->second.as<std::string>();
        }
        if (mode_str == "inventory") {
            request.mode = AlgorithmMode::inventory;
            request.payload = InventoryPayload{};
        } else {
            request.mode = AlgorithmMode::direct;
            request.payload = DirectPayload{source_enchants};
        }

        // ── Algorithm ─────────────────────────────────────────────────────
        {
            auto it = root.find("algorithm");
            if (it != root.end()) request.algorithm = it->second.as<std::string>();
        }
        if (request.algorithm.empty())
            request.algorithm = "greedy";

        // ── Platform ──────────────────────────────────────────────────────
        std::string plat;
        {
            auto it = root.find("platform");
            if (it != root.end()) plat = it->second.as<std::string>();
        }
        if (plat == "bedrock")
            request.forge_config.platform = MCE::Bedrock;
        else
            request.forge_config.platform = MCE::Java;

        // ── Max solutions ─────────────────────────────────────────────────
        int32_t max_sol = 0;
        {
            auto it = root.find("max_solutions");
            if (it != root.end()) max_sol = it->second.as<int32_t>();
        }
        if (max_sol > 0)
            request.search_config.max_solutions = max_sol;

        // ── Solve ─────────────────────────────────────────────────────────
        auto result = c->impl.solve(request);

        // ── Format result as raw JSON ─────────────────────────────────────
        Json::Object root_obj;
        root_obj["success"] = Json(result.success);
        root_obj["algorithm"] = Json(result.algorithm_used);
        root_obj["computation_time_ms"] = Json(result.computation_time_ms);

        Json::Array sol_arr;
        for (const auto& sol : result.solutions) {
            Json::Object s;
            s["total_exp_level_cost"] = Json(sol.total_exp_level_cost);
            s["total_exp_cost"] = Json(sol.total_exp_cost);
            s["is_success"] = Json(sol.is_success);
            s["step_count"] = Json(static_cast<int64_t>(sol.steps.size()));
            sol_arr.push_back(Json(s));
        }
        root_obj["solutions"] = Json(sol_arr);

        auto json_out = Json(root_obj).to_string(Json::Pretty);
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
