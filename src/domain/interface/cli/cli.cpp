#include "cli.h"
#include "common/i18n/Language.h"
#include "domain/interface/cli/CLIParser.h"
#include "BuildConfig.h"
#include "common/utils/StringUtils.hpp"
#include <iostream>
#include <stdexcept>

// Stringify helper so BESQ_MAX_SOLUTIONS appears literally in the help text
#define BESQ_STR2(x) #x
#define BESQ_STR(x)  BESQ_STR2(x)

// ============================================================================
// Help text
// ============================================================================
std::string get_cli_help_text(const std::string &program_name) {
    return
        tr_fmt("cli.help.usage", program_name) + "\n"
        "   " + tr_fmt("cli.help.usage_export", program_name) + "\n"
        "   " + tr_fmt("cli.help.usage_noargs", program_name) + "\n"
        "\n"
        + tr("cli.help.options_header") + ":\n"
        "  -h, --help              " + tr("cli.help.help_desc") + "\n"
        "  -V, --version           " + tr("cli.help.version_desc") + "\n"
        "  --algorithm <name>      " + tr("cli.help.algorithm_desc") + "\n"
        "  --source <list>         " + tr("cli.help.source_desc") + "\n"
        "  --target <spec>         " + tr("cli.help.target_desc") + "\n"
        "  --mode <mode>           " + tr("cli.help.mode_desc") + "\n"
        "  --platform <platform>   " + tr("cli.help.platform_desc") + "\n"
        "  --solutions <n>         " + tr_fmt("cli.help.solutions_desc", BESQ_STR(BESQ_MAX_SOLUTIONS)) + "\n"
        "  --format <format>       " + tr("cli.help.format_desc") + "\n"
        "  --input <file>          " + tr("cli.help.input_desc") + "\n"
        "  --output <file>         " + tr("cli.help.output_desc") + "\n"
        "  --registry-dir <dir>    " + tr("cli.help.registry_dir_desc") + "\n"
        "  --registries <list>     " + tr("cli.help.registries_desc") + "\n"
        "  --registry-edit <ops>   " + tr("cli.help.registry_edit_desc") + "\n"
        "  --export-registry <path>\n"
        "                           " + tr("cli.help.export_registry_desc") + "\n"
        "  --config <pairs>        " + tr("cli.help.config_desc") + "\n"
        "  --memory <MB|auto>      " + tr("cli.help.memory_desc") + "\n"
        "  --algo-dir <dir>        " + tr("cli.help.algo_dir_desc") + "\n"
        "  --max-time <seconds>    " + tr("cli.help.max_time_desc") + "\n"
        "  --lang <code>           " + tr("cli.help.lang_desc") + "\n"
        "  -v, --verbose           " + tr("cli.help.verbose_desc") + "\n"
        "\n"
        + tr("cli.help.ench_format_header") + ":\n"
        "  " + tr("cli.help.ench_format_id_level") + "\n"
        "  " + tr("cli.help.ench_format_nsid_level") + "\n"
        "  " + tr("cli.help.ench_format_colon") + "\n";
}

// ============================================================================
// Main CLI argument parsing → CLIConfig
// ============================================================================

CLIConfig parse_cli(int argc, char *argv[]) {
    std::string prog = argc > 0 ? argv[0] : "besq";
    auto args = CLIParser::parse(argc, argv);

    CLIConfig config;
    if (args.empty()) {
        config.help = true;
        std::cout << get_cli_help_text(prog) << std::endl;
        return config;
    }

    for (const auto &arg : args) {
        const auto &key = arg.key;

        // Boolean flags
        if (key == "help") {
            config.help = true;
            std::cout << get_cli_help_text(prog) << std::endl;
            continue;
        }
        if (key == "version" || key == "V") {
            config.version = true;
            std::cout << prog << " version " << BESQ_VERSION << std::endl;
            continue;
        }
        if (key == "verbose") {
            config.verbose = true;
            continue;
        }
        const auto &value = arg.value;

        if (key == "mode") {
            if (value != "direct" && value != "inventory") {
                throw std::runtime_error(tr_fmt("cli.err.invalid_mode", value));
            }
            config.mode = value;
        } else if (key == "target") {
            config.target = value;
        } else if (key == "source") {
            config.source = value;
        } else if (key == "registry-dir") {
            config.registry_dir = value;
        } else if (key == "registries") {
            config.registries = value;
        } else if (key == "registry-edit") {
            if (value.empty())
                throw std::runtime_error(tr_fmt("cli.err.empty_registry_edit"));
            // Basic format validation: must contain at least one ':'
            auto ops = string_utils::split(value, ';');
            for (const auto& op : ops) {
                if (op.find(':') == std::string::npos)
                    throw std::runtime_error(
                        tr_fmt("cli.err.invalid_registry_edit", op));
            }
            config.registry_edit = value;
        } else if (key == "list-algorithms") {
            config.list_algorithms = true;
        } else if (key == "algo-dir") {
            if (value.empty())
                throw std::runtime_error(tr_fmt("cli.err.empty_algo_dir"));
            config.algo_dir = value;
        } else if (key == "export-registry") {
            if (value.empty())
                throw std::runtime_error(tr_fmt("cli.err.empty_export_registry"));
            config.export_registry = value;
        } else if (key == "config") {
            config.config_pairs = value;
            // Validate syntax and recognize keys; actual application
            // to ForgeConfig happens later via apply_config_pairs().
            if (value.empty())
                throw std::runtime_error(tr_fmt("cli.err.empty_config"));
            auto pairs = string_utils::split(value, ',');
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
        } else if (key == "input") {
            config.input = value;
        } else if (key == "output") {
            config.output = value;
        } else if (key == "platform") {
            if (value != "java" && value != "bedrock" && value != "auto") {
                throw std::runtime_error(tr_fmt("cli.err.invalid_platform", value));
            }
            config.platform = value;
        } else if (key == "format") {
            if (value != "text" && value != "compact" && value != "json") {
                throw std::runtime_error(tr_fmt("cli.err.invalid_format", value));
            }
            config.format = value;
        } else if (key == "algorithm") {
            config.algorithm = value;
        } else if (key == "solutions") {
            try {
                int n = std::stoi(value);
                if (n < 0) throw std::runtime_error(tr("cli.err.solutions_negative"));
                if (n > static_cast<int>(BESQ_MAX_SOLUTIONS))
                    throw std::runtime_error(tr_fmt("cli.err.solutions_out_of_range", BESQ_MAX_SOLUTIONS));
                config.solutions = n;
            } catch (const std::runtime_error &) {
                throw;
            } catch (const std::exception &) {
                throw std::runtime_error(tr_fmt("cli.err.invalid_solutions", value));
            }
        } else if (key == "memory") {
            if (value == "auto") {
                config.memory_mb = 0;
            } else {
                try {
                    int n = std::stoi(value);
                    if (n <= 0) throw std::runtime_error(tr("cli.err.memory_not_positive"));
                    if (n > 1048576) throw std::runtime_error(tr("cli.err.memory_out_of_range"));
                    config.memory_mb = n;
                } catch (const std::runtime_error &) {
                    throw;
                } catch (const std::exception &) {
                    throw std::runtime_error(tr_fmt("cli.err.invalid_memory", value));
                }
            }
        } else if (key == "max-time") {
            try {
                int n = std::stoi(value);
                if (n < 0) throw std::runtime_error(tr("cli.err.max_time_negative"));
                config.max_time = n;
            } catch (const std::runtime_error &) {
                throw;
            } catch (const std::exception &) {
                throw std::runtime_error(tr_fmt("cli.err.invalid_max_time", value));
            }
        } else if (key == "lang") {
            if (value.empty())
                throw std::runtime_error(tr("cli.err.empty_lang"));
            config.lang = value;
        } else {
            throw std::runtime_error(tr_fmt("cli.err.unknown_option", key));
        }
    }

    // Validate required arguments
    // --target or --export-registry or --list-algorithms is required
    if (!config.help && !config.version) {
        if (config.target.empty() && !config.export_registry.has_value() && !config.list_algorithms)
            throw std::runtime_error(tr("cli.err.missing_target"));
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
