#include "AppConfig.h"
#include "BuildConfig.h"
#include "common/log/log.hpp"
#include "common/utils/ParserUtils.hpp"
#include "domain/interface/cli/cli.h"
#include "domain/interface/cli/RegistryEditor.h"
#include "domain/interface/api/SolvePipeline.h"
#include "domain/interface/parsers/EnchParser.h"
#include "domain/interface/parsers/EnchInfoParser.h"
#include "domain/interface/parsers/ItemParser.h"
#include "domain/interface/parsers/ParserUtilsDomain.hpp"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/Item.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "orchestration/components/CompactAdapter.h"
#include "orchestration/components/EnchSerializer.h"
#include "orchestration/components/OutputFormatter.h"
#include "orchestration/components/RawTypeAdapter.h"
#include "builtin/DataLoader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

// ====================================================================
// Registry setup helpers
// ====================================================================

/// Load all registry files from a directory (flat files or MC official).
static void load_registry_dir(
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    EquipmentCategoryRegistry& cat_reg,
    const std::string& dir_path)
{
    namespace fs = std::filesystem;
    fs::path dir(dir_path);

    if (!fs::exists(dir))
        throw std::runtime_error("Registry directory not found: " + dir_path);
    if (!fs::is_directory(dir))
        throw std::runtime_error("Not a directory: " + dir_path);

    // MC Official structure (data/<ns>/enchantment/…): handle as one unit
    if (fs::is_directory(dir / "data")) {
        auto [raw_ench, raw_eq] = EnchInfoParser::parse(dir_path);

        std::vector<std::string> custom_categories;
        for (const auto& eq : raw_eq) {
            if (cat_reg.get_id(eq.category) < 0)
                custom_categories.push_back(eq.category);
        }
        if (!custom_categories.empty())
            cat_reg.initialize(custom_categories);

        auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, cat_reg);
        for (auto& info : ench_infos)
            ench_reg.add(info);

        auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, cat_reg);
        for (auto& eq : equipments)
            eq_reg.add(eq);
        return;
    }

    // Flat directory of individual .json / .csv files: load each one
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        try {
            auto [raw_ench, raw_eq] = EnchInfoParser::parse(entry.path());

            std::vector<std::string> custom_categories;
            for (const auto& eq : raw_eq) {
                if (cat_reg.get_id(eq.category) < 0)
                    custom_categories.push_back(eq.category);
            }
            if (!custom_categories.empty())
                cat_reg.initialize(custom_categories);

            auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, cat_reg);
            for (auto& info : ench_infos)
                ench_reg.add(info);

            auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, cat_reg);
            for (auto& eq : equipments)
                eq_reg.add(eq);
        } catch (const std::exception& e) {
            LOG_DEBUG("Skipping non-registry entry '%s': %s",
                      entry.path().string().c_str(), e.what());
        }
    }
}

} // anonymous namespace

// ====================================================================
// main
// ====================================================================
int main(int argc, char* argv[]) try {
    // ── Configuration & CLI parsing ──
    auto app_cfg = AppConfig::load();
    auto config = parse_cli(argc, argv);

    // ── Logger setup ──
    Logger::instance().set_level(
        app_cfg.log_level >= 3 ? LogLevel::Error
      : app_cfg.log_level >= 2 ? LogLevel::Warn
      : app_cfg.log_level >= 1 ? LogLevel::Info
      :                          LogLevel::Debug);
    Logger::instance().set_retention(app_cfg.log_retention);

    // ── Early exit for --help / --version ──
    if (config.help || config.version)
        return 0;

    // ── Initialise registries with built-in vanilla data ──
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    EquipmentCategoryRegistry cat_reg;
    besq::data::load_builtin_data(cat_reg, ench_reg, eq_reg);
    LOG_INFO("Loaded built-in vanilla data (%zu enchantments, %zu equipment)",
             ench_reg.size(), eq_reg.size());

    // ── Algorithm loader (built-in strategies) ──
    algorithm::AlgorithmLoader algo_loader;
    algo_loader.load_builtin();

    // Load external algorithm plugins
    {
        auto algo_path = config.algo_dir.value_or(app_cfg.algo_dir);
        if (std::filesystem::is_directory(algo_path))
            algo_loader.scan_and_load(algo_path);
    }

    // ── --list-algorithms ──
    if (config.list_algorithms) {
        auto algos = algo_loader.list();
        std::cout << "Available algorithm strategies (" << algos.size() << "):\n";
        for (const auto& name : algos)
            std::cout << "  " << name << "\n";
        return 0;
    }

    // ── Load external registry data ──
    if (config.registry_dir)
        load_registry_dir(ench_reg, eq_reg, cat_reg, *config.registry_dir);

    if (config.registries) {
        for (const auto& reg : ParserUtils::split_string(*config.registries, ',')) {
            if (reg.empty()) continue;
            if (std::filesystem::exists(reg)) {
                auto [raw_ench, raw_eq] = EnchInfoParser::parse(reg);
                auto infos = RawTypeAdapter::resolve_ench_info(raw_ench, cat_reg);
                for (auto& info : infos) ench_reg.add(info);
                auto eqs = RawTypeAdapter::resolve_equipment(raw_eq, cat_reg);
                for (auto& eq : eqs) eq_reg.add(eq);
            }
        }
    }

    // ── Runtime registry edits ──
    if (config.registry_edit)
        apply_registry_edits(*config.registry_edit, ench_reg, eq_reg, cat_reg);

    // ── Registry export ──
    if (config.export_registry) {
        bool ok = EnchSerializer::export_json(*config.export_registry, ench_reg, eq_reg, cat_reg);
        if (!ok)
            throw std::runtime_error("Failed to export registry to: " + *config.export_registry);
        LOG_INFO("Registry exported to %s", config.export_registry->c_str());
        return 0;
    }

    // ── Solve ──
    if (!config.target.empty()) {
        // Build SolveInput from CLI config
        auto target_spec = ItemParser::parse(config.target);
        auto target_item = build_target(target_spec, ench_reg, eq_reg);

        EnchSet source_enchants;
        if (!config.source.empty()) {
            auto source_specs = EnchParser::parse(config.source);
            source_enchants  = build_enchset(source_specs, ench_reg);
        }

        SolveInput solve_input;
        solve_input.target_item = target_item;
        solve_input.source_enchantments = source_enchants;
        if (config.platform == "auto")
            solve_input.forge_config.platform = MCE::Java;
        else
            solve_input.forge_config.platform = MCE::Java;
        if (config.platform == "bedrock")
            solve_input.forge_config.platform = MCE::Bedrock;
        apply_config_pairs(config.config_pairs, solve_input.forge_config);
        solve_input.search_config.max_solutions = config.solutions;
        solve_input.algorithm = config.algorithm;
        solve_input.is_inventory_mode = (config.mode == "inventory");

        // Run the pipeline
        auto result = detail::SolvePipeline::run(
            solve_input, algo_loader, ench_reg, eq_reg, cat_reg);

        // Format output
        std::string output;
        if (config.format == "json")
            output = OutputFormatter::format_json(
                result.solutions, ench_reg, cat_reg, config.mode);
        else if (config.format == "compact")
            output = OutputFormatter::format_compact(
                result.solutions, ench_reg, cat_reg, config.mode);
        else
            output = OutputFormatter::format_verbose(
                result.solutions, ench_reg, cat_reg, config.mode);

        if (config.output) {
            std::ofstream out(*config.output);
            if (!out)
                throw std::runtime_error("Failed to open output file: " + *config.output);
            out << output;
        } else {
            std::cout << output;
        }
    }

    return 0;

} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
}
