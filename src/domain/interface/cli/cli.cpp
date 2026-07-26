#include "cli.h"
#include "common/i18n/Language.h"
#include "common/utils/cli/CLIParser.h"
#include "common/utils/cli/CLIParserI18n.h"
#include "BuildConfig.h"
#include "common/utils/StringUtils.hpp"
#include <iostream>
#include <stdexcept>

// ── UserI18nTranslator — adapts parser diagnostics to project i18n ──
class UserI18nTranslator {
public:
    std::string operator()(const Diagnostic& diag) const {
        using enum ParseErrorCode;
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

    std::string operator()(std::string_view key) const {
        return tr(key);
    }
};

// ============================================================================
// Help text
// ============================================================================
std::string get_cli_help_text(const std::string &program_name) {
    return format_help(BESQ_OPTIONS, program_name, UserI18nTranslator{});
}

// ============================================================================
// Main CLI argument parsing → CLIConfig
// ============================================================================

CLIConfig parse_cli(int argc, char *argv[]) {
    std::string prog = argc > 0 ? argv[0] : "besq";

    // Parse with new type-safe parser
    auto result = parse(BESQ_OPTIONS, std::span<const char*>((const char**)argv, argc));

    // Handle diagnostics
    UserI18nTranslator user_i18n;
    if (!result.diagnostics.empty()) {
        // Check for help/version first
        CLIConfig config;
        BIND_CLI(config, result);

        if (config.help) {
            std::cout << get_cli_help_text(prog) << std::endl;
            return config;
        }
        if (config.version) {
            std::cout << prog << " version " << BESQ_VERSION << std::endl;
            return config;
        }

        // Print errors
        for (auto& d : result.diagnostics) {
            std::cerr << user_i18n(d) << std::endl;
        }

        // If fatal errors, throw
        bool fatal = false;
        for (auto& d : result.diagnostics) {
            if (d.code == ParseErrorCode::required_missing ||
                d.code == ParseErrorCode::unknown_option ||
                d.code == ParseErrorCode::invalid_value ||
                d.code == ParseErrorCode::missing_value) {
                fatal = true;
                break;
            }
        }
        if (fatal) {
            throw std::runtime_error(tr("cli.err.parse_failed"));
        }
    }

    // Bind to CLIConfig
    CLIConfig config;
    BIND_CLI(config, result);

    // Check for --config explicitly set to empty
    {
        auto& raw_cfg = std::get<18>(result.value);
        if (raw_cfg.has_value() && raw_cfg->empty())
            throw std::runtime_error(tr("cli.err.empty_config"));
    }

    // Handle --memory (supports "auto" for auto-detection)
    {
        auto& raw_mem = std::get<20>(result.value);
        if (raw_mem.has_value()) {
            if (*raw_mem == "auto") {
                config.memory_mb = 0;
            } else {
                try {
                    int n = std::stoi(*raw_mem);
                    if (n <= 0)
                        throw std::runtime_error(tr("cli.err.memory_not_positive"));
                    if (n > 1048576)
                        throw std::runtime_error(tr("cli.err.memory_out_of_range"));
                    config.memory_mb = n;
                } catch (const std::runtime_error&) {
                    throw;
                } catch (const std::exception&) {
                    throw std::runtime_error(tr_fmt("cli.err.invalid_memory", *raw_mem));
                }
            }
        }
    }

    // Business validation (preserve existing validation logic)
    if (!config.target.empty()) {
        if (config.mode != "direct" && config.mode != "inventory") {
            throw std::runtime_error(tr_fmt("cli.err.invalid_mode", config.mode));
        }
    }

    if (config.platform != "java" && config.platform != "bedrock" && config.platform != "auto") {
        throw std::runtime_error(tr_fmt("cli.err.invalid_platform", config.platform));
    }

    if (config.format != "text" && config.format != "compact" && config.format != "json") {
        throw std::runtime_error(tr_fmt("cli.err.invalid_format", config.format));
    }

    if (config.solutions < 0) {
        throw std::runtime_error(tr("cli.err.solutions_negative"));
    }
    if (config.solutions > static_cast<int>(BESQ_MAX_SOLUTIONS)) {
        throw std::runtime_error(tr_fmt("cli.err.solutions_exceed_max", BESQ_MAX_SOLUTIONS));
    }

    if (!config.config_pairs.empty()) {
        auto pairs = string_utils::split(config.config_pairs, ',');
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

    if (config.registry_edit.has_value()) {
        if (config.registry_edit->empty())
            throw std::runtime_error(tr("cli.err.empty_registry_edit"));
        auto ops = string_utils::split(*config.registry_edit, ';');
        for (const auto& op : ops) {
            if (op.find(':') == std::string::npos)
                throw std::runtime_error(tr_fmt("cli.err.invalid_registry_edit", op));
        }
    }

    if (config.algo_dir.has_value() && config.algo_dir->empty())
        throw std::runtime_error(tr_fmt("cli.err.empty_algo_dir"));

    if (config.export_registry.has_value() && config.export_registry->empty())
        throw std::runtime_error(tr_fmt("cli.err.empty_export_registry"));

    if (!config.help && !config.version) {
        if (config.target.empty() && !config.export_registry.has_value()) {
            throw std::runtime_error(tr("cli.err.missing_target_or_export"));
        }
    }

    return config;
}

// ============================================================================
// apply_config_pairs — parse --config value and apply to ForgeConfig
// ============================================================================

void apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg) {
    if (config_pairs.empty()) return;
    auto pairs = string_utils::split(config_pairs, ',');
    for (const auto& pair : pairs) {
        auto eq = pair.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Invalid config pair: '" + pair + "'. Expected key=value format.\n");
        std::string k = pair.substr(0, eq);
        std::string v = pair.substr(eq + 1);
        bool val = (v == "true");
        if (k == "ignore-cost-cap")
            cfg.ignore_cost_cap = val;
        else if (k == "ignore-penalty-cost")
            cfg.ignore_penalty_cost = val;
        else if (k == "ignore-repair-cost")
            cfg.ignore_repair_cost = val;
        // Unknown keys are already rejected in parse_cli()
    }
}
