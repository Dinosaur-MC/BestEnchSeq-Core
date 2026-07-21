#include "api/SolvePipeline.h"
#include "besq/besq.h"
#include "cli/cli.h"
#include "cli/RegistryEditor.h"
#include "config/AppConfig.h"
#include "log/log.hpp"
#include "adapters/OutputFormatter.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "parsers/ParserUtilsDomain.hpp"
#include "resolvers/InventoryResolver.h"
#include "utils/ParserUtils.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

// ====================================================================
// build_solve_input — Convert CLIConfig + BesqContext into SolveInput
// ====================================================================
static SolveInput build_solve_input(
    const CLIConfig& config,
    const BesqContext& ctx,
    int64_t default_memory_mb)
{
    SolveInput input;

    // ── Target item (equipment + inline desired enchantments) ─────────
    auto target_spec = ItemParser::parse(config.target);
    input.target_item = build_target(target_spec, ctx.enchantments(), ctx.equipment());

    // ── Source enchantments (what the item already has) ───────────────
    if (!config.source.empty()) {
        auto source_specs = EnchParser::parse(config.source);
        input.source_enchantments = build_enchset(source_specs, ctx.enchantments());
    }

    // ── Forge configuration ──────────────────────────────────────────
    if (config.platform == "auto")
        input.forge_config.platform = MCE::All;
    else
        input.forge_config.platform = ParserUtils::parse_platform(config.platform);
    apply_config_pairs(config.config_pairs, input.forge_config);

    // ── Search configuration ─────────────────────────────────────────
    input.search_config.max_solutions = config.solutions;
    {
        int64_t effective_mb = config.memory_mb > 0
            ? static_cast<int64_t>(config.memory_mb)
            : default_memory_mb;
        input.search_config.memory_mb = static_cast<int32_t>(effective_mb);
    }
    if (config.max_time > 0)
        input.search_config.max_search_time = std::chrono::seconds(config.max_time);

    // ── Algorithm & mode ─────────────────────────────────────────────
    input.algorithm = config.algorithm;
    input.is_inventory_mode = (config.mode == "inventory");

    // ── Inventory mode: resolve inventory file ────────────────────────
    if (input.is_inventory_mode) {
        if (!config.input)
            throw std::runtime_error("Input file required for inventory mode");

        auto inv = InventoryResolver::resolve(
            std::filesystem::path(*config.input),
            ctx.enchantments(),
            ctx.equipment());

        for (const auto& w : inv.warnings)
            LOG_WARN("Inventory: %s", w.c_str());

        input.extra_items = std::move(inv.items);
    }

    return input;
}

// ====================================================================
// load_registry_dir — Load all registry files from a directory
// ====================================================================
static void load_registry_dir(BesqContext& ctx, const std::string& dir_path) {
    namespace fs = std::filesystem;
    fs::path dir(dir_path);

    if (!fs::exists(dir))
        throw std::runtime_error("Registry directory not found: " + dir_path);
    if (!fs::is_directory(dir))
        throw std::runtime_error("Not a directory: " + dir_path);

    // MC Official structure (data/<ns>/enchantment/…): handle as one unit
    if (fs::is_directory(dir / "data")) {
        ctx.load_file(dir_path);
        return;
    }

    // Flat directory of individual .json / .csv files: load each one
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        try {
            ctx.load_file(entry.path().string());
        } catch (const std::exception& e) {
            LOG_DEBUG("Skipping non-registry entry '%s': %s",
                      entry.path().string().c_str(), e.what());
        }
    }
}

} // anonymous namespace

// ====================================================================
// main — thin CLI wrapper around BesqContext
// ====================================================================
int main(int argc, char* argv[]) try {
    // ── Configuration & CLI parsing ──────────────────────────────────
    auto app_cfg = AppConfig::load();
    auto config = parse_cli(argc, argv);

    // ── Logger setup ─────────────────────────────────────────────────
    Logger::instance().set_level(
        app_cfg.log_level >= 3 ? LogLevel::Error
      : app_cfg.log_level >= 2 ? LogLevel::Warn
      : app_cfg.log_level >= 1 ? LogLevel::Info
      :                          LogLevel::Debug);
    Logger::instance().set_retention(app_cfg.log_retention);

    // ── Early exit for --help / --version ────────────────────────────
    if (config.help || config.version)
        return 0;

    // ── Create BesqContext and load data ─────────────────────────────
    BesqContext ctx;
    ctx.load_builtin();

    if (config.registry_dir)
        load_registry_dir(ctx, *config.registry_dir);

    if (config.registries) {
        for (const auto& reg : ParserUtils::split_string(*config.registries, ',')) {
            if (reg.empty()) continue;
            if (std::filesystem::exists(reg)) {
                ctx.load_file(reg);
            }
        }
    }

    // ── Runtime registry edits ───────────────────────────────────────
    if (config.registry_edit)
        apply_registry_edits(*config.registry_edit,
            const_cast<EnchantmentRegistry&>(ctx.enchantments()),
            const_cast<EquipmentRegistry&>(ctx.equipment()),
            const_cast<EquipmentCategoryRegistry&>(ctx.categories()));

    // ── Load external algorithm strategies ────────────────────────────
    // Default search path: ./algorithms/ (silently skipped if absent)
    {
        auto algo_path = config.algo_dir.value_or("algorithms");
        if (std::filesystem::is_directory(algo_path)) {
            size_t n = ctx.load_plugins(algo_path);
            LOG_INFO("Loaded %zu external algorithm(s) from %s",
                     n, algo_path.c_str());
        }
    }

    // ── Registry export (works without --target) ─────────────────────
    if (config.export_registry) {
        if (!ctx.export_registry(*config.export_registry))
            throw std::runtime_error("Failed to export registry to: " + *config.export_registry);
    }

    // ── Solve ────────────────────────────────────────────────────────
    if (!config.target.empty()) {
        auto input = build_solve_input(config, ctx, app_cfg.memory_mb);
        auto result = ctx.solve(input);

        // Format output (pass config.mode to match current behaviour for
        // both "direct" and "inventory" display).
        std::string output;
        if (config.format == "json")
            output = OutputFormatter::format_json(
                result.solutions, ctx.enchantments(), ctx.categories(), config.mode);
        else if (config.format == "compact")
            output = OutputFormatter::format_compact(
                result.solutions, ctx.enchantments(), ctx.categories(), config.mode);
        else
            output = OutputFormatter::format_verbose(
                result.solutions, ctx.enchantments(), ctx.categories(), config.mode);

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
