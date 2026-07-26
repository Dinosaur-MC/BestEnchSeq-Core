#include "CLIApp.h"
#include "common/i18n/Language.h"
#include "common/utils/cli/CLIParser.hpp"
#include "BuildConfig.h"
#include "common/utils/StringUtils.hpp"
#include <iostream>
#include <stdexcept>

// ============================================================================
// Parser options table (hidden from header)
// ============================================================================

namespace {
using namespace cli;

const auto BESQ_OPTIONS = OptionTable{
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

} // anonymous namespace

// ============================================================================
// i18n translator — bridges cli::Diagnostic to project tr()
// ============================================================================

struct CLI::UserI18nTranslator {
    std::string operator()(const cli::Diagnostic& diag) const {
        using enum cli::ParseErrorCode;
        switch (diag.code) {
            case unknown_option:
                return tr_fmt("cli.err.unknown_option", diag.arg);
            case missing_value:
                return tr_fmt("cli.err.missing_value",
                              diag.option_name.value_or(std::string_view{}));
            case invalid_value:
                return tr_fmt("cli.err.invalid_value", diag.arg);
            case required_missing:
                return tr_fmt("cli.err.required_missing",
                              diag.option_name.value_or(std::string_view{}));
            case unexpected_positional:
                return tr_fmt("cli.err.unexpected_arg", diag.arg);
            default:
                return tr("cli.err.unknown");
        }
    }

    std::string operator()(std::string_view key) const { return tr(key); }
};

// ============================================================================
// Help text
// ============================================================================

std::string CLI::help_text(std::string_view program_name) {
    return cli::CLIParser(BESQ_OPTIONS, UserI18nTranslator{}).format_help(program_name);
}

// ============================================================================
// Main CLI argument parsing
// ============================================================================

CLI::Config CLI::parse(int argc, char* argv[]) {
    std::string prog = argc > 0 ? argv[0] : "besq";

    auto cli_parser = cli::CLIParser(BESQ_OPTIONS, UserI18nTranslator{});
    auto result = cli_parser.parse(
        std::span<const char*>((const char**)argv, argc));

    if (!result.diagnostics.empty()) {
        Config early_cfg = bind(result);

        if (early_cfg.help) {
            std::cout << help_text(prog) << std::endl;
            return early_cfg;
        }
        if (early_cfg.version) {
            std::cout << prog << " version " << BESQ_VERSION << std::endl;
            return early_cfg;
        }

        for (auto& msg : result.messages)
            std::cerr << msg << std::endl;

        for (auto& d : result.diagnostics) {
            switch (d.code) {
                case cli::ParseErrorCode::required_missing:
                case cli::ParseErrorCode::unknown_option:
                case cli::ParseErrorCode::invalid_value:
                case cli::ParseErrorCode::missing_value:
                    throw std::runtime_error(tr("cli.err.parse_failed"));
                default: break;
            }
        }
    }

    Config cfg = bind(result);

    // Post-bind: --memory (supports "auto")
    {
        auto& raw_mem = std::get<20>(result.value);
        if (raw_mem.has_value()) {
            if (*raw_mem == "auto") {
                cfg.memory_mb = 0;
            } else {
                try {
                    int n = std::stoi(*raw_mem);
                    if (n <= 0) throw std::runtime_error(tr("cli.err.memory_not_positive"));
                    if (n > 1048576) throw std::runtime_error(tr("cli.err.memory_out_of_range"));
                    cfg.memory_mb = n;
                } catch (const std::runtime_error&) { throw;
                } catch (const std::exception&) {
                    throw std::runtime_error(tr_fmt("cli.err.invalid_memory", *raw_mem));
                }
            }
        }
    }

    // Post-bind: --config (empty check)
    {
        auto& raw_cfg = std::get<18>(result.value);
        if (raw_cfg.has_value() && raw_cfg->empty())
            throw std::runtime_error(tr("cli.err.empty_config"));
    }

    // Business validation
    if (!cfg.target.empty()) {
        if (cfg.mode != "direct" && cfg.mode != "inventory")
            throw std::runtime_error(tr_fmt("cli.err.invalid_mode", cfg.mode));
    }
    if (cfg.platform != "java" && cfg.platform != "bedrock" && cfg.platform != "auto")
        throw std::runtime_error(tr_fmt("cli.err.invalid_platform", cfg.platform));
    if (cfg.format != "text" && cfg.format != "compact" && cfg.format != "json")
        throw std::runtime_error(tr_fmt("cli.err.invalid_format", cfg.format));
    if (cfg.solutions < 0)
        throw std::runtime_error(tr("cli.err.solutions_negative"));
    if (cfg.solutions > static_cast<int>(BESQ_MAX_SOLUTIONS))
        throw std::runtime_error(tr_fmt("cli.err.solutions_exceed_max", BESQ_MAX_SOLUTIONS));

    if (!cfg.config_pairs.empty()) {
        auto pairs = string_utils::split(cfg.config_pairs, ',');
        for (const auto& pair : pairs) {
            auto eq = pair.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error(tr_fmt("cli.err.invalid_config_pair", pair));
            if (eq == 0)
                throw std::runtime_error(tr_fmt("cli.err.empty_config_key", pair));
            auto k = pair.substr(0, eq);
            auto v = pair.substr(eq + 1);
            if (v.empty())
                throw std::runtime_error(tr_fmt("cli.err.empty_config_value", pair));
            if (k != "ignore-cost-cap" && k != "ignore-penalty-cost" && k != "ignore-repair-cost")
                throw std::runtime_error(tr_fmt("cli.err.unknown_config_key", k));
            if (v != "true" && v != "false")
                throw std::runtime_error(tr_fmt("cli.err.invalid_config_value", k, v));
        }
    }

    if (cfg.registry_edit.has_value()) {
        if (cfg.registry_edit->empty())
            throw std::runtime_error(tr("cli.err.empty_registry_edit"));
        auto ops = string_utils::split(*cfg.registry_edit, ';');
        for (const auto& op : ops) {
            if (op.find(':') == std::string::npos)
                throw std::runtime_error(tr_fmt("cli.err.invalid_registry_edit", op));
        }
    }

    if (cfg.algo_dir.has_value() && cfg.algo_dir->empty())
        throw std::runtime_error(tr_fmt("cli.err.empty_algo_dir"));
    if (cfg.export_registry.has_value() && cfg.export_registry->empty())
        throw std::runtime_error(tr_fmt("cli.err.empty_export_registry"));

    if (!cfg.help && !cfg.version) {
        if (cfg.target.empty() && !cfg.export_registry.has_value())
            throw std::runtime_error(tr("cli.err.missing_target_or_export"));
    }

    return cfg;
}

// ============================================================================
// apply_config_pairs — parse --config value and apply to ForgeConfig
// ============================================================================

void CLI::apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg) {
    if (config_pairs.empty()) return;
    auto pairs = string_utils::split(config_pairs, ',');
    for (const auto& pair : pairs) {
        auto eq = pair.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Invalid config pair: '" + pair + "'. Expected key=value format.\n");
        std::string k = pair.substr(0, eq);
        std::string v = pair.substr(eq + 1);
        bool val = (v == "true");
        if (k == "ignore-cost-cap")       cfg.ignore_cost_cap = val;
        else if (k == "ignore-penalty-cost") cfg.ignore_penalty_cost = val;
        else if (k == "ignore-repair-cost")  cfg.ignore_repair_cost = val;
    }
}
