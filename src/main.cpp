#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include "algorithm/strategies/Strategies.h"
#include "adapters/CompactAdapter.h"
#include "cli/cli.h"
#include "config/AppConfig.h"
#include "data/DataLoader.h"
#include "data/EmbeddedData.h"
#include "parsers/EnchInfoParser.h"
#include "log/log.hpp"
#include "adapters/OutputFormatter.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "parsers/ParserUtilsDomain.hpp"
#include "resolvers/ItemResolver.h"
#include "resolvers/RegistryResolver.h"
#include "registries/AlgorithmRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "io/json.h"
#include "utils/ParserUtils.hpp"
#include "types/AlgorithmTypes.h"


#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace {

// Built-in data subdirectory (resolved relative to exe or CWD)
inline constexpr auto BUILTIN_DATA_DIR = "data/builtin";

void load_custom_data(
    const std::filesystem::path &data_pack_dir,
    const EquipmentCategoryRegistry &cat_reg,
    EnchantmentRegistry &ench_reg,
    EquipmentRegistry &eq_reg
) {
    if (!std::filesystem::exists(data_pack_dir))
        throw std::runtime_error("Data pack directory not found: " + data_pack_dir.string());

    // Parse new data as raw
    auto [raw_ench, raw_eq] = EnchInfoParser::parse(data_pack_dir);

    // Merge with already-loaded data and re-initialize
    {
        auto existing = ench_reg.get_instances();
        std::vector<EnchInfo> combined;
        combined.reserve(existing.size() + raw_ench.size());
        for (const auto &info : existing)
            combined.push_back(info);
        auto new_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
        for (const auto &info : new_infos)
            combined.push_back(info);
        ench_reg.initialize(combined);
    }
    {
        auto &existing = eq_reg.get_instances();
        std::vector<Equipment> combined;
        combined.reserve(existing.size() + raw_eq.size());
        for (const auto &eq : existing)
            combined.push_back(eq);
        auto new_eq = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
        for (const auto &eq : new_eq)
            combined.push_back(eq);
        eq_reg.initialize(combined);
    }
}

void register_builtin_algorithms(AlgorithmRegistry &registry) {
    registry.register_algorithm("greedy", [] { return std::make_unique<GreedyAlgorithm>(); });
    registry.register_algorithm("dfs", [] { return std::make_unique<DFSAlgorithm>(); });
    registry.register_algorithm("astar", [] { return std::make_unique<AStarAlgorithm>(); });
    registry.register_algorithm("penalty_balance",
                                [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
    registry.register_algorithm("hierarchical",
                                [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
    registry.register_algorithm("idastar",
                                [] { return std::make_unique<IDAStarAlgorithm>(); });
    registry.register_algorithm("hamming",
                                [] { return std::make_unique<HammingAlgorithm>(); });
    registry.register_algorithm("difficulty_first",
                                [] { return std::make_unique<DiffFirstAlgorithm>(); });
}

// Helper: parse an inventory JSON file into an ItemCollection.
// Uses registries directly for name→ID resolution (with "minecraft:" fallback).
ItemCollection parse_inventory(
    const std::filesystem::path& path,
    const EnchantmentRegistry& ench_reg,
    const EquipmentRegistry& eq_reg
) {
    std::string content = ParserUtils::read_file(path);
    Json root = Json::parse(content);
    if (root.type() != JsonType::Object) return {};

    Json::Value root_val = root.get_value();
    if (!std::holds_alternative<Json::Object>(root_val)) return {};
    Json::Object root_obj = std::get<Json::Object>(root_val);

    auto items_it = root_obj.find("items");
    if (items_it == root_obj.end()) return {};

    Json::Value items_val = items_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(items_val)) return {};
    Json::Array items_arr = std::get<Json::Array>(items_val);

    ItemCollection result;
    for (const Json& item_json : items_arr) {
        if (item_json.type() != JsonType::Object) continue;
        Json::Value item_val = item_json.get_value();
        if (!std::holds_alternative<Json::Object>(item_val)) continue;
        Json::Object item_obj = std::get<Json::Object>(item_val);

        std::string type = ParserUtils::get_json_string(item_obj, "type");
        if (type.empty()) {
            LOG_WARN("Warning: item missing 'type' field, skipping");
            continue;
        }

        // Parse enchantments
        EnchSet ench_set;
        auto ench_it = item_obj.find("enchants");
        if (ench_it != item_obj.end()) {
            Json::Value enchants_val = ench_it->second.get_value();
            if (std::holds_alternative<Json::Array>(enchants_val)) {
                Json::Array ench_arr = std::get<Json::Array>(enchants_val);
                for (const Json& ench_json : ench_arr) {
                    if (ench_json.type() != JsonType::Object) continue;
                    Json::Value ench_val = ench_json.get_value();
                    if (!std::holds_alternative<Json::Object>(ench_val)) continue;
                    Json::Object ench_obj = std::get<Json::Object>(ench_val);

                    std::string ench_id_str = ParserUtils::get_json_string(ench_obj, "id");
                    int32_t ench_level = ParserUtils::get_json_int(ench_obj, "level");
                    if (ench_level < 1) ench_level = 1;

                    int32_t ench_id = ench_reg.get_id(ench_id_str);
                    if (ench_id >= 0) {
                        ench_set.emplace(ench_id, ench_level);
                    } else {
                        LOG_WARN("Warning: unknown enchantment '%s' in item, skipping", ench_id_str.c_str());
                    }
                }
            }
        }

        int32_t prior_penalty = ParserUtils::get_json_int(item_obj, "prior_penalty");
        int32_t priority = ParserUtils::get_json_int(item_obj, "priority");
        if (priority <= 0) priority = 99;

        if (type == "book") {
            auto& item = result.emplace_back(ench_set, prior_penalty);
            item.priority = priority;
        } else if (type == "equipment") {
            std::string equip_id = ParserUtils::get_json_string(item_obj, "id");
            int32_t eq_id = eq_reg.get_id(equip_id);

            if (eq_id >= 0) {
                const Equipment& equip = eq_reg.get(eq_id);
                int32_t durability = ParserUtils::get_json_int(item_obj, "durability");
                if (durability <= 0) {
                    durability = equip.max_durability;
                }
                auto& item = result.emplace_back(equip, ench_set, prior_penalty, durability);
                item.priority = priority;
            } else {
                LOG_WARN("Warning: equipment '%s' not found in registry, treating as book", equip_id.c_str());
                result.emplace_back(ench_set, prior_penalty);
            }
        } else {
            LOG_WARN("Warning: unknown item type '%s', skipping", type.c_str());
        }
    }

    return result;
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    try {
        // Load environment-backed config (CLI overrides take precedence later)
        auto app_cfg = AppConfig::load();
        auto config = parse_cli(argc, argv);

        // ── Configure global Logger ─────────────────────────────────────────
        Logger::instance().set_level(
            app_cfg.log_level >= 3 ? LogLevel::Error
          : app_cfg.log_level >= 2 ? LogLevel::Warn
          : app_cfg.log_level >= 1 ? LogLevel::Info
          :                          LogLevel::Debug);
        Logger::instance().set_retention(app_cfg.log_retention);

        if (config.help || config.version) {
            return 0;
        }

        // ── Load data ────────────────────────────────────────────────────────

        // Resolve builtin data directory: prefer path relative to executable,
        // fall back to CWD-relative path for development builds.
        auto builtin_data_dir = std::filesystem::absolute(argv[0]).parent_path() / BUILTIN_DATA_DIR;
        if (!std::filesystem::exists(builtin_data_dir)) {
            builtin_data_dir = std::filesystem::path(BUILTIN_DATA_DIR);
        }

        // Local registries (no globals)
        EquipmentCategoryRegistry cat_reg;
        cat_reg.initialize();

        EnchantmentRegistry ench_reg;
        EquipmentRegistry eq_reg;

        besq::data::load_builtin_data(
            cat_reg, ench_reg, eq_reg, builtin_data_dir);

        if (config.data_pack) {
            load_custom_data(std::filesystem::path(*config.data_pack),
                             cat_reg, ench_reg, eq_reg);
        }

        // ── Resolve CLI specs → domain items ─────────────────────────────
        auto target_spec = ItemParser::parse(config.target);
        auto target_item = build_target(target_spec, ench_reg, eq_reg);

        EnchSet source_ench;
        if (!config.source.empty()) {
            auto source_specs = EnchParser::parse(config.source);
            source_ench = build_enchset(source_specs, ench_reg);
        }

        EnchSet target_ench;
        if (!target_spec.inline_enchants.empty()) {
            target_ench = build_enchset(target_spec.inline_enchants, ench_reg);
        }

        // ── Register and create algorithm (local registry, no singleton) ──
        AlgorithmRegistry algo_reg;
        register_builtin_algorithms(algo_reg);
        auto algo = algo_reg.create(config.algorithm);
        if (!algo) {
            auto available = algo_reg.list();
            std::string msg = "Unknown algorithm: '" + config.algorithm + "'. Available: ";
            for (size_t i = 0; i < available.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += available[i];
            }
            throw std::runtime_error(msg);
        }

        // Check algorithm mode support
        AlgorithmMode algo_mode = (config.mode == "inventory")
            ? AlgorithmMode::inventory
            : AlgorithmMode::direct;
        if (!(algo->supported_mode() & algo_mode)) {
            throw std::runtime_error("Algorithm '" + config.algorithm +
                "' does not support '" + config.mode + "' mode");
        }

        // ── Boundary: domain → compact ────────────────────────────────────
        // CompactAdapter is fully static — no instance needed
        ForgeConfig forge_config;
        if (config.platform == "auto") {
            forge_config.platform = MCE::All;
        } else {
            forge_config.platform = ParserUtils::parse_platform(config.platform);
        }

        AlgorithmInput algo_input;
        // Keep these alive for the recall call later
        EnchSet recall_source_ench;
        ItemStack recall_target_item;
        ItemCollection recall_available_items;

        if (config.mode == "direct") {
            auto resolved = ItemResolver::resolve(
                target_item, source_ench, target_ench, ench_reg);
            recall_source_ench = resolved.source_ench;
            recall_target_item = resolved.target_item;
            recall_available_items = resolved.available_items;
            algo_input = CompactAdapter::apply(resolved, ench_reg);
        } else {
            // inventory mode
            if (!config.input.has_value())
                throw std::runtime_error("Input file required for inventory mode");

            ItemCollection available_items = parse_inventory(
                std::filesystem::path(*config.input), ench_reg, eq_reg);

            // Stable sort by priority (lower = more preferred)
            std::stable_sort(available_items.begin(), available_items.end(),
                [](const ItemStack& a, const ItemStack& b) {
                    return a.priority < b.priority;
                });

            ResolvedInput resolved{
                target_item, source_ench, target_ench, std::move(available_items)};
            recall_source_ench = resolved.source_ench;
            recall_target_item = resolved.target_item;
            recall_available_items = resolved.available_items;
            algo_input = CompactAdapter::apply(resolved, ench_reg);
        }

        algo_input.config.platform = forge_config.platform;
        algo_input.mode = algo_mode;

        // ── Search config from CLI ────────────────────────────────────────
        algo_input.search.max_solutions = config.solutions;
        algo_input.search.memory_mb = static_cast<int32_t>(
            config.memory_mb > 0 ? config.memory_mb : app_cfg.memory_mb);

        // ── Feasibility pre-check (inventory mode) ─────────────────────────
        bool feasible = true;
        if (config.mode == "inventory" && !algo->simulate(algo_input)) {
            LOG_INFO("simulate: target not reachable from given items");
            feasible = false;
        }

        LOG_INFO("Starting algorithm: %s", config.algorithm.c_str());

        // Execute (compact-only algorithm layer)
        std::vector<EnchSolution> solutions;
        if (feasible) {
            AlgorithmExecutor executor(std::move(algo));
            executor.start(algo_input);
            executor.wait();

            // ── Verbose diagnostics ────────────────────────────────────────
            if (config.verbose || app_cfg.verbose) {
                auto diag = executor.get_diagnostics(0);
                LOG_INFO(
                    "nodes_visited=%lld  nodes_pruned=%lld  steps_forged=%lld  progress=%.1f%%",
                    diag.nodes_visited, diag.nodes_pruned, diag.steps_forged,
                    diag.progress * 100.0);
            }

            // ── Boundary: compact → domain ────────────────────────────────
            solutions = CompactAdapter::recall(executor.output(), algo_input,
                                        recall_source_ench, recall_target_item,
                                        recall_available_items);
        }

        // Format output
        std::string output_text;
        if (config.format == "json") {
            output_text = OutputFormatter::format_json(solutions, ench_reg, cat_reg, config.mode);
        } else if (config.format == "compact") {
            output_text = OutputFormatter::format_compact(solutions, ench_reg, cat_reg, config.mode);
        } else {
            output_text = OutputFormatter::format_verbose(solutions, ench_reg, cat_reg, config.mode);
        }

        if (config.output) {
            std::ofstream out(*config.output);
            if (!out.is_open()) {
                throw std::runtime_error("Failed to open output file: " + *config.output);
            }
            out << output_text;
        } else {
            std::cout << output_text;
        }

        DiagnosticsService::instance().flush();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
