#pragma once
#include "domain/algorithm/types/ConfigTypes.h"
#include <optional>
#include <string>
#include "common/utils/cli/CLIParser.h"
#include "common/utils/cli/CLIParserI18n.h"

// ─── CLI data types ──────────────────────────────────────────────────────

struct CLIConfig {
    std::string algorithm = "hamming";
    std::string mode      = "direct";
    std::string target;
    std::string source;
    std::string lang;        // --lang (locale override)
    std::string config_pairs; // raw --config value
    std::optional<std::string> input;
    std::optional<std::string> registry_dir;
    std::optional<std::string> registries;
    std::optional<std::string> registry_edit;   // --registry-edit
    std::optional<std::string> export_registry; // --export-registry
    std::optional<std::string> algo_dir;        // --algo-dir
    std::string platform = "auto";
    std::optional<std::string> output;
    std::string format   = "text";
    int solutions        = 1;
    int memory_mb        = 0;
    int max_time         = 0; // --max-time <seconds> (0 = unlimited)
    bool verbose         = false;
    bool help            = false;
    bool list_algorithms = false; // --list-algorithms
    bool version         = false; // --version / -V
};

// Compile-time option table for BESQ CLI
inline const auto BESQ_OPTIONS = OptionTable{
    Flag    {.long_name = "help",             .short_name = 'h', .help_key = "cli.help.help_desc"},
    Flag    {.long_name = "verbose",          .short_name = 'v', .help_key = "cli.help.verbose_desc"},
    Flag    {.long_name = "version",          .short_name = 'V', .help_key = "cli.help.version_desc"},
    Flag    {.long_name = "list-algorithms",                      .help_key = "List available algorithms"},
    Option<std::string>{.long_name = "algorithm",                 .help_key = "cli.help.algorithm_desc",    .default_v = std::string("hamming")},
    Option<std::string>{.long_name = "target",                    .help_key = "cli.help.target_desc"},
    Option<std::string>{.long_name = "source",                    .help_key = "cli.help.source_desc"},
    Option<std::string>{.long_name = "mode",                      .help_key = "cli.help.mode_desc",         .default_v = std::string("direct")},
    Option<std::string>{.long_name = "platform",                  .help_key = "cli.help.platform_desc",     .default_v = std::string("auto")},
    Option<std::string>{.long_name = "format",                    .help_key = "cli.help.format_desc",       .default_v = std::string("text")},
    Option<std::string>{.long_name = "lang",                      .help_key = "cli.help.lang_desc"},
    Option<std::string>{.long_name = "input",                     .help_key = "cli.help.input_desc"},
    Option<std::string>{.long_name = "output",                    .help_key = "cli.help.output_desc"},
    Option<std::string>{.long_name = "registry-dir",              .help_key = "cli.help.registry_dir_desc"},
    Option<std::string>{.long_name = "registries",                .help_key = "cli.help.registries_desc"},
    Option<std::string>{.long_name = "registry-edit",             .help_key = "cli.help.registry_edit_desc"},
    Option<std::string>{.long_name = "export-registry",           .help_key = "cli.help.export_registry_desc"},
    Option<std::string>{.long_name = "algo-dir",                  .help_key = "cli.help.algo_dir_desc"},
    Option<std::string>{.long_name = "config",                    .help_key = "cli.help.config_desc"},
    Option<int>        {.long_name = "solutions",    .short_name = 's', .help_key = "cli.help.solutions_desc", .default_v = 1},
    Option<std::string>{.long_name = "memory",                    .help_key = "cli.help.memory_desc"},
    Option<int>        {.long_name = "max-time",                  .help_key = "cli.help.max_time_desc"},
};

// BIND_CLI macro: maps BESQ_OPTIONS indices to CLIConfig fields
// Index order matches BESQ_OPTIONS declaration order:
//   0=help, 1=verbose, 2=version, 3=list-algorithms,
//   4=algorithm, 5=target, 6=source, 7=mode, 8=platform, 9=format,
//   10=lang, 11=input, 12=output, 13=registry-dir, 14=registries,
//   15=registry-edit, 16=export-registry, 17=algo-dir, 18=config,
//   19=solutions, 20=memory, 21=max-time
#define BIND_CLI(cfg, result) \
    do { \
        auto& _v = (result).value; \
        cfg.help                = std::get<0>(_v); \
        cfg.verbose             = std::get<1>(_v); \
        cfg.version             = std::get<2>(_v); \
        cfg.list_algorithms     = std::get<3>(_v); \
        cfg.algorithm           = std::get<4>(_v).value_or(std::string("hamming")); \
        cfg.target              = std::get<5>(_v).value_or(std::string()); \
        cfg.source              = std::get<6>(_v).value_or(std::string()); \
        cfg.mode                = std::get<7>(_v).value_or(std::string("direct")); \
        cfg.platform            = std::get<8>(_v).value_or(std::string("auto")); \
        cfg.format              = std::get<9>(_v).value_or(std::string("text")); \
        cfg.lang                = std::get<10>(_v).value_or(std::string()); \
        cfg.input               = std::get<11>(_v); \
        cfg.output              = std::get<12>(_v); \
        cfg.registry_dir        = std::get<13>(_v); \
        cfg.registries          = std::get<14>(_v); \
        cfg.registry_edit       = std::get<15>(_v); \
        cfg.export_registry     = std::get<16>(_v); \
        cfg.algo_dir            = std::get<17>(_v); \
        cfg.config_pairs        = std::get<18>(_v).value_or(std::string()); \
        cfg.solutions           = std::get<19>(_v).value_or(1); \
        cfg.memory_mb           = 0;  /* handled post-BIND in parse_cli */ \
        cfg.max_time            = std::get<21>(_v).value_or(0); \
    } while(0)

// ─── Forward declarations (registries included only in .cpp) ─────
class EnchantmentRegistry;
class EquipmentRegistry;

// ─── Business CLI parsing ────────────────────────────────────────────────

/// Parse CLI arguments into a CLIConfig. Internally uses CLIParser::parse()
/// for raw key-value extraction, then applies business validation.
CLIConfig parse_cli(int argc, char *argv[]);

/// Business help text describing all options and their semantics.
std::string get_cli_help_text(const std::string &program_name = "besq");

/// Parse a --config value and apply recognized key=value pairs to a ForgeConfig.
///
/// Recognized keys: ignore-cost-cap, ignore-penalty-cost, ignore-repair-cost.
/// Each value must be "true" or "false".
/// Throws std::runtime_error on unrecognized keys, malformed syntax, or invalid values.
void apply_config_pairs(const std::string &config_pairs, algorithm::ForgeConfig &cfg);
