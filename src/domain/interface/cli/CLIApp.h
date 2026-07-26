#pragma once

#include "common/utils/cli/CLICommon.h"  // cli::ParseResult (for bind())
#include "domain/algorithm/types/ConfigTypes.h"
#include <optional>
#include <string>

// ============================================================================
// CLI — Command-Line Interface Facade
// ============================================================================
//
// Encapsulates business-level CLI argument parsing and validation.
// Uses the generic cli::CLIParser internally for type-safe option parsing.
//
// Usage:
//   auto cfg = CLI::parse(argc, argv);
//   std::cout << CLI::help_text("besq");
//   CLI::apply_config_pairs(pairs, forge_cfg);

class CLI {
public:
    struct Config {
        std::string algorithm = "hamming";
        std::string mode      = "direct";
        std::string target;
        std::string source;
        std::string lang;
        std::string config_pairs;
        std::optional<std::string> input;
        std::optional<std::string> registry_dir;
        std::optional<std::string> registries;
        std::optional<std::string> registry_edit;
        std::optional<std::string> export_registry;
        std::optional<std::string> algo_dir;
        std::string platform = "auto";
        std::optional<std::string> output;
        std::string format   = "text";
        int solutions        = 1;
        int memory_mb        = 0;
        int max_time         = 0;
        bool verbose         = false;
        bool help            = false;
        bool list_algorithms = false;
        bool version         = false;
    };

    static Config parse(int argc, char* argv[]);
    static std::string help_text(std::string_view program_name = "besq");
    static void apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg);

private:
    struct UserI18nTranslator;

    template<typename... Entries>
    static Config bind(const cli::ParseResult<Entries...>& result) {
        const auto& v = result.value;
        Config cfg;
        cfg.help            = std::get<0>(v);
        cfg.verbose         = std::get<1>(v);
        cfg.version         = std::get<2>(v);
        cfg.list_algorithms = std::get<3>(v);
        cfg.algorithm       = std::get<4>(v).value_or(Config{}.algorithm);
        cfg.target          = std::get<5>(v).value_or(Config{}.target);
        cfg.source          = std::get<6>(v).value_or(Config{}.source);
        cfg.mode            = std::get<7>(v).value_or(Config{}.mode);
        cfg.platform        = std::get<8>(v).value_or(Config{}.platform);
        cfg.format          = std::get<9>(v).value_or(Config{}.format);
        cfg.lang            = std::get<10>(v).value_or(Config{}.lang);
        cfg.input           = std::get<11>(v);
        cfg.output          = std::get<12>(v);
        cfg.registry_dir    = std::get<13>(v);
        cfg.registries      = std::get<14>(v);
        cfg.registry_edit   = std::get<15>(v);
        cfg.export_registry = std::get<16>(v);
        cfg.algo_dir        = std::get<17>(v);
        cfg.config_pairs    = std::get<18>(v).value_or(Config{}.config_pairs);
        cfg.solutions       = std::get<19>(v).value_or(Config{}.solutions);
        cfg.memory_mb       = 0;
        cfg.max_time        = std::get<21>(v).value_or(Config{}.max_time);
        return cfg;
    }
};

// ── Backward-compatible aliases ─────────────────────────────────────────

using CLIConfig = CLI::Config;

inline CLIConfig parse_cli(int argc, char* argv[]) { return CLI::parse(argc, argv); }
inline std::string get_cli_help_text(const std::string& p = "besq") { return CLI::help_text(p); }
inline void apply_config_pairs(const std::string& p, algorithm::ForgeConfig& c) { CLI::apply_config_pairs(p, c); }
