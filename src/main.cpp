#include "AppConfig.h"
#include "common/log/log.hpp"
#include "common/utils/StringUtils.hpp"
#include "domain/interface/interface.h"
#include "domain/interface/cli/EnchParser.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/interface/cli/RegistryEditor.h"
#include "domain/business/business.h"
#include "domain/algorithm/algorithm.h"
#include "domain/orchestration/orchestration.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/LocaleDetector.h"
#include "common/i18n/Language.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// ====================================================================
// main
// ====================================================================
int main(int argc, char* argv[]) try {
    // ── Configuration ──
    auto app_cfg = AppConfig::load();

    // ── i18n setup (before CLI parsing so help text is translated) ──
    auto& lang_mgr = LanguageManager::instance();
    register_builtin_translations(lang_mgr);

    // Initial locale: env var > system detect
    {
        const char* env_lang = std::getenv("BESQ_LANG");
        std::string lang_code = env_lang ? env_lang : detect_system_locale();
        lang_mgr.select(lang_mgr.resolve_locale(lang_code));
    }

    // ── CLI parsing (may override --lang) ──
    auto config = parse_cli(argc, argv);

    // If --lang was explicitly set, re-select
    if (!config.lang.empty())
        lang_mgr.select(lang_mgr.resolve_locale(config.lang));

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

    // ── Initialize ProfileManager with built-in vanilla data ──
    ProfileManager profiles;
    ProfileLoader loader;
    auto& builtin = profiles.create(NSID("builtin:vanilla"));
    loader.load_builtin(builtin);
    profiles.activate(NSID("builtin:vanilla"));
    LOG_INFO("%s", tr("main.msg.loaded_builtin").c_str());

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
        std::cout << tr_fmt("cli.msg.list_algorithms", algos.size()) << "\n";
        for (const auto& name : algos)
            std::cout << "  " << name << "\n";
        return 0;
    }

    // ── Load external registry data ──
    if (config.registry_dir) {
        ManageRequest req;
        req.action = ManageRequest::Action::LoadFile;
        req.file_path = *config.registry_dir;
        ManagePipeline::run(profiles, loader, req);
    }

    if (config.registries) {
        for (const auto& reg : string_utils::split(*config.registries, ',')) {
            if (reg.empty()) continue;
            if (std::filesystem::exists(reg)) {
                ManageRequest req;
                req.action = ManageRequest::Action::LoadFile;
                req.file_path = reg;
                ManagePipeline::run(profiles, loader, req);
            }
        }
    }

    // ── Runtime registry edits ──
    if (config.registry_edit)
        apply_registry_edits(*config.registry_edit, profiles.active());

    // ── Registry export ──
    if (config.export_registry) {
        bool ok = EnchSerializer::export_json(*config.export_registry, profiles.active());
        if (!ok)
            throw std::runtime_error(tr_fmt("main.err.export_failed", *config.export_registry));
        LOG_INFO("%s", tr_fmt("main.msg.registry_exported", *config.export_registry).c_str());
        return 0;
    }

    // ── Solve ──
    if (!config.target.empty()) {
        auto& profile = profiles.active();

        Item target_item = ItemParser::parse(config.target, profile.ench(), profile.eq());

        SolveRequest request;
        request.target_item = target_item;
        request.mode = (config.mode == "inventory") ? AlgorithmMode::inventory : AlgorithmMode::direct;

        if (!config.source.empty()) {
            auto source_enchants = EnchParser::parse(config.source, profile.ench());
            request.payload = DirectPayload{source_enchants};
        } else {
            request.payload = DirectPayload{};
        }

        if (config.platform == "bedrock")
            request.forge_config.platform = MCE::Bedrock;
        else
            request.forge_config.platform = MCE::Java;

        apply_config_pairs(config.config_pairs, request.forge_config);
        request.search_config.max_solutions = config.solutions;
        request.algorithm = config.algorithm;

        // Run the pipeline
        auto result = SolvePipeline::run(profile, request, algo_loader);

        // Format output
        std::string output;
        if (config.format == "json")
            output = OutputFormatter::format_json(result.solutions, profile, request.mode);
        else if (config.format == "compact")
            output = OutputFormatter::format_compact(result.solutions, profile, request.mode);
        else
            output = OutputFormatter::format_verbose(result.solutions, profile, request.mode);

        if (config.output) {
            std::ofstream out(*config.output);
            if (!out)
                throw std::runtime_error(tr_fmt("main.err.output_failed", *config.output));
            out << output;
        } else {
            std::cout << output;
        }
    }

    return 0;

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
