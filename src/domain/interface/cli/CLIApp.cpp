#include "domain/interface/cli/CLIApp.h"
#include "domain/interface/cli/EnchParser.h"
#include "domain/interface/cli/InventoryParser.h"
#include "domain/interface/cli/InventorySchema.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/interface/BesqContext.h"
#include "domain/business/types/EnchInfo.h"
#include "common/CommonTypes.h"
#include "common/i18n/Language.h"
#include "common/i18n/LocaleDetector.h"
#include "common/utils/cli/CLIParser.hpp"
#include "BuildConfig.h"
#include "common/utils/StringUtils.hpp"
#include "common/utils/EnvUtil.hpp"
#include "common/log/log.hpp"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

// ============================================================================
// CLIApp — Application runner
// ============================================================================

std::string CLIApp::detect_target(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string_view(argv[i]) == "--api")
            return argv[i + 1];
    }
    return "cli";
}

CLIApp::CLIApp()
    : _ctx()
{
    _ctx.load_builtin();
}

CLIApp::~CLIApp() {
    // RAII safety net: whatever path run() took (including exceptions thrown
    // mid-run), flush CLI output + drain the async Logger queue while this
    // process is still alive.  The implicit flush at process exit is
    // unreliable here — see flush_output().  Runs after every per-feature
    // flush, so it is normally a no-op; it exists to catch the paths where
    // run() returns/throws without printing a banner.
    flush_output();
}

void CLIApp::flush_output() noexcept {
    // None of these can realistically throw (cout/cerr exceptions are disabled
    // by default; besq::log::flush is a bounded busy-wait), but the dtor must
    // stay noexcept, so swallow anything that does.
    try {
        besq::log::flush();   // async Logger's queued console mirror lines
        std::cout.flush();    // the CLI's own buffered output
        std::cerr.flush();    // belt-and-suspenders (cerr is unit-buffered)
    } catch (...) {}
}

// ============================================================================
// apply_lang — Select language from env / locale / CLI flag
// ============================================================================

void CLIApp::apply_lang(int argc, char* argv[]) {
    auto& lang_mgr = LanguageManager::instance();

    // 1. Select base language from BESQ_LANG env var or system locale.
    //    LanguageManager::select() handles case/_/- normalization internally
    //    and will try to load from langs/ on demand.
    std::string base_code = get_env<std::string>("BESQ_LANG", detect_system_locale());
    if (!lang_mgr.select(base_code))
        lang_mgr.select(lang_mgr.resolve_locale(base_code));

    // 2. --lang CLI flag override.
    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        std::string override_code;
        if (a.starts_with("--lang="))
            override_code = std::string(a.substr(7));
        else if (a == "--lang" && i + 1 < argc)
            override_code = argv[i + 1];
        else
            continue;

        if (!lang_mgr.select(override_code)) {
            auto avail = lang_mgr.available();
            std::string avail_str = string_utils::join(avail, ", ");
            std::cerr << tr_fmt("cli.err.invalid_lang", override_code, avail_str) << std::endl;
        }
        break;
    }
}

int CLIApp::run(int argc, char* argv[]) {
    // Note: apply_lang() is called by main.cpp before run(), so the
    // correct language is already selected when we get here.

    // 1. Parse CLI args
    auto config = CLIApp::parse(argc, argv);

    // Re-select language if --lang was explicitly set
    // (LanguageManager::select handles case/_/- normalization)
    if (!config.lang.empty()) {
        auto& lang_mgr = LanguageManager::instance();
        if (!lang_mgr.select(config.lang))
            lang_mgr.select(lang_mgr.resolve_locale(config.lang));
    }

    if (config.help) {
        std::cout << help_text(argv[0]) << std::endl;
        return 0;
    }
    if (config.version) {
        std::cout << BESQ_PROJECT_NAME << " v" << BESQ_VERSION << std::endl;
        return 0;
    }
    if (config.brief_usage) {
        return 0;
    }

    // 2. Domain-wide auto-load (built-in → profiles → algorithms → langs).
    //    Explicit --algo-dir below appends to the auto-loaded set (a later
    //    same-name plugin replaces the earlier registration).
    _ctx.auto_load();

    // 2b. --algo-dir explicit plugin directory (appends to auto-load)
    if (config.algo_dir)
        _ctx.load_algorithms(*config.algo_dir);

    // 3. --list-algorithms
    if (config.list_algorithms) {
        auto algos = _ctx.list_algorithms();
        std::cout << tr_fmt("cli.msg.list_algorithms", algos.size()) << "\n";
        for (const auto& name : algos)
            std::cout << "  " << name << "\n";
        flush_output();  // make the list durable now, not at exit-time
        return 0;
    }

    // Algorithm name early validation (U10)
    if (!config.algorithm.empty() && !config.help && !config.version && !config.list_algorithms) {
        auto algos = _ctx.list_algorithms();
        if (std::find(algos.begin(), algos.end(), config.algorithm) == algos.end()) {
            std::string avail_str;
            for (size_t i = 0; i < algos.size(); ++i) {
                if (i > 0) avail_str += ", ";
                avail_str += algos[i];
            }
            throw std::runtime_error(tr_fmt("pipeline.err.unknown_algo",
                config.algorithm, avail_str));
        }
    }

    // 4. Profile selection
    //    auto_load() (step 1) already scanned `<exe_dir>/profiles` (safe
    //    no-op when missing).  --profile-dir overrides: re-scan the chosen
    //    directory.  activate_profile must run before any --import/--edit so
    //    those operations land in the chosen profile.
    if (config.profile_dir)
        _ctx.set_profiles_dir(*config.profile_dir);
    _ctx.load_profiles();
    if (config.profile)
        _ctx.activate_profile(*config.profile);

    // 4b. --list-profiles / --list-langs
    if (config.list_profiles) {
        auto profiles = _ctx.list_profiles();
        std::cout << tr_fmt("cli.msg.list_profiles", profiles.size()) << "\n";
        const auto active = _ctx.active_profile();
        for (const auto& p : profiles) {
            std::cout << "  " << p;
            if (p == active)
                std::cout << " " << tr("cli.msg.list_profiles_active");
            std::cout << "\n";
        }
        flush_output();
        return 0;
    }
    if (config.list_langs) {
        auto langs = LanguageManager::instance().available();
        std::cout << tr_fmt("cli.msg.list_langs", langs.size()) << "\n";
        for (const auto& l : langs)
            std::cout << "  " << l << "\n";
        flush_output();
        return 0;
    }

    // 5. Profile data operations (target the active profile)
    if (config.import_files) {
        for (const auto& f : string_utils::split(*config.import_files, ','))
            if (!f.empty())
                _ctx.import_profile(f);
    }

    if (config.edit_ops)
        CLIApp::apply_edits(*config.edit_ops, _ctx);

    // 6. Profile export (`--export -` dumps JSON to stdout)
    if (config.export_path) {
        if (*config.export_path == "-") {
            std::cout << _ctx.export_profile_to_string() << "\n";
            flush_output();
        } else {
            bool ok = _ctx.export_profile(*config.export_path);
            if (!ok) throw std::runtime_error(
                tr_fmt("main.err.export_failed", *config.export_path));
            LOG_INFO("%s", tr_fmt("main.msg.profile_exported", *config.export_path).c_str());
        }
        return 0;
    }

    // 7. Publish profile as self-contained file
    if (config.publish) {
        std::string version = config.publish_version.value_or("dev");
        std::string tag     = config.publish_tag.value_or("");
        std::string out     = config.output.value_or(*config.publish + ".json");
        bool ok = _ctx.publish_profile(*config.publish, version, tag, out);
        if (!ok)
            throw std::runtime_error(tr_fmt("main.err.publish_failed", *config.publish));
        LOG_INFO("%s", tr_fmt("main.msg.published", out).c_str());
        return 0;
    }

    // 8. Resume from checkpoint (self-contained: no target/source needed)
    if (config.resume) {
        auto ck = _ctx.solve_from_checkpoint(*config.resume);
        auto output = _ctx.format(ck.result, ck.mode, config.format);
        if (config.output) {
            std::ofstream out(*config.output);
            if (!out) throw std::runtime_error(
                tr_fmt("main.err.output_failed", *config.output));
            out << output;
        } else {
            std::cout << output;
        }
        flush_output();
        return 0;
    }

    // 9. Solve
    if (!config.target.empty() || config.input) {
        SolveRequest request = CLIApp::build_solve_request(config, _ctx);

        OutputFormatter::set_show_nsid(config.verbose);
        auto result = _ctx.solve(request);

        // When the pipeline determines the target is unreachable (conflicting
        // enchantments, missing prerequisites, etc.), it returns an empty
        // solution set.  Surface this to the user instead of printing nothing.
        if (!result.success && result.solutions.empty()) {
            throw std::runtime_error(tr("cli.err.unreachable_target"));
        }

        auto output = _ctx.format(result, request.mode, config.format);

        if (config.output) {
            std::ofstream out(*config.output);
            if (!out) throw std::runtime_error(
                tr_fmt("main.err.output_failed", *config.output));
            out << output;
        } else {
            std::cout << output;
        }
        flush_output();
    }

    return 0;
}

// ============================================================================
// Parser options table (hidden from header)
// ============================================================================

namespace {
using namespace cli;

/// True when the user literally passed `--profile` (either `--profile X` or
/// `--profile=X`).  `config.profile` alone cannot tell: CLIParser applies the
/// default_v ("builtin:vanilla") during parse(), so an omitted --profile is
/// value-identical to an explicit `--profile builtin:vanilla`.  The token
/// scan runs before any `--` terminator (everything after `--` is positional).
bool has_profile_token(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--")
            break;
        if (a == "--profile" || a.starts_with("--profile="))
            return true;
    }
    return false;
}

const auto BESQ_OPTIONS = OptionTable{
    // ── basic ──
    Flag    {.long_name = "help",              .short_name = 'h', .help_key = "cli.help.help_desc",            .help_group = "cli.help.group_basic"},
    Flag    {.long_name = "verbose",           .short_name = 'v', .help_key = "cli.help.verbose_desc",         .help_group = "cli.help.group_output"},
    Flag    {.long_name = "version",           .short_name = 'V', .help_key = "cli.help.version_desc",         .help_group = "cli.help.group_info"},
    Flag    {.long_name = "list-algorithms",                            .help_key = "List available algorithms", .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "algorithm",                       .help_key = "cli.help.algorithm_desc",  .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "target",                          .help_key = "cli.help.target_desc",     .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "source",                          .help_key = "cli.help.source_desc",     .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "mode",                            .help_key = "cli.help.mode_desc",       .help_group = "cli.help.group_basic",  .default_v = std::string("direct")},
    Option<std::string>{.long_name = "platform",                        .help_key = "cli.help.platform_desc",   .help_group = "cli.help.group_platform", .default_v = std::string("auto")},
    Option<std::string>{.long_name = "format",                          .help_key = "cli.help.format_desc",     .help_group = "cli.help.group_output", .default_v = std::string("text")},
    Option<std::string>{.long_name = "lang",                            .help_key = "cli.help.lang_desc",       .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "input",                           .help_key = "cli.help.input_desc",      .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "output",                          .help_key = "cli.help.output_desc",     .help_group = "cli.help.group_output"},
    Option<std::string>{.long_name = "import",                          .help_key = "cli.help.import_desc",     .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "edit",                            .help_key = "cli.help.edit_desc",       .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "export",                          .help_key = "cli.help.export_desc",     .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "algo-dir",                        .help_key = "cli.help.algo_dir_desc",   .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "config",                          .help_key = "cli.help.config_desc",     .help_group = "cli.help.group_platform"},
    Option<int>        {.long_name = "solutions",          .short_name = 's', .help_key = "cli.help.solutions_desc", .help_group = "cli.help.group_basic", .default_v = 1},
    Option<std::string>{.long_name = "memory",                           .help_key = "cli.help.memory_desc",    .help_group = "cli.help.group_advanced"},
    Option<int>        {.long_name = "max-time",                         .help_key = "cli.help.max_time_desc",  .help_group = "cli.help.group_advanced"},
    Option<int>        {.long_name = "max-threads",      .short_name = 'j', .help_key = "cli.help.max_threads_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "profile",          .help_key = "cli.help.profile_desc",       .help_group = "cli.help.group_profile", .default_v = std::string("builtin:vanilla")},
    Option<std::string>{.long_name = "profile-dir",      .help_key = "cli.help.profile_dir_desc",   .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "publish",          .help_key = "cli.help.publish_desc",       .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "publish-version",  .help_key = "cli.help.publish_version_desc", .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "publish-tag",      .help_key = "cli.help.publish_tag_desc",   .help_group = "cli.help.group_profile"},
    Option<std::string>{.long_name = "algo-opt",         .help_key = "cli.help.algo_opt_desc",      .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "resume",           .help_key = "cli.help.resume_desc",        .help_group = "cli.help.group_advanced"},
    Flag    {.long_name = "list-profiles",               .help_key = "cli.help.list_profiles_desc", .help_group = "cli.help.group_info"},
    Flag    {.long_name = "list-langs",                  .help_key = "cli.help.list_langs_desc",    .help_group = "cli.help.group_info"},
};

} // anonymous namespace

// ============================================================================
// i18n translator — bridges cli::Diagnostic to project tr()
// ============================================================================

struct CLIApp::UserI18nTranslator {
    std::string operator()(const cli::Diagnostic& diag) const {
        using enum cli::ParseErrorCode;
        switch (diag.code) {
            case unknown_option:
                return tr_fmt("cli.err.unknown_option", diag.arg);
            case missing_value:
                return tr_fmt("cli.err.missing_value",
                              diag.option_name.value_or(std::string{}));
            case invalid_value:
                return tr_fmt("cli.err.invalid_value", diag.arg);
            case required_missing:
                return tr_fmt("cli.err.required_missing",
                              diag.option_name.value_or(std::string{}));
            case unexpected_positional:
                return tr_fmt("cli.err.unexpected_arg", diag.arg);
            case duplicate_option:
                return tr_fmt("cli.err.duplicate_option", diag.option_name.value_or(std::string{}));
            case flag_takes_no_value:
                return tr_fmt("cli.err.flag_no_value", diag.option_name.value_or(std::string{}));
            default:
                return tr("cli.err.unknown");
        }
    }

    std::string operator()(std::string_view key) const {
        // 唯一带 {0} 占位符的帮助条目：注入编译期 BESQ_MAX_SOLUTIONS
        if (key == "cli.help.solutions_desc")
            return tr_fmt(key, BESQ_MAX_SOLUTIONS);
        return tr(key);
    }
};

// ============================================================================
// Help text
// ============================================================================

std::string CLIApp::help_text(std::string_view program_name) {
    std::string r = cli::CLIParser(BESQ_OPTIONS, UserI18nTranslator{}).format_help(program_name);

    // Examples
    r += "\nExamples:\n";
    r += "  " + std::string(program_name) + " --target diamond_sword[sharpness=5,knockback=2]\n";
    r += "  " + std::string(program_name) + " --source \"protection=4\" --target diamond_chestplate[protection=5,unbreaking=3]\n";
    r += "  " + std::string(program_name) + " --export out.json\n\n";

    // Enchantment format reference
    r += tr("cli.help.ench_format_header") + "\n";
    r += "  " + tr("cli.help.ench_format_id_level") + "\n";
    r += "  " + tr("cli.help.ench_format_nsid_level") + "\n";
    r += "  " + tr("cli.help.ench_format_colon") + "\n";

    return r;
}

// ============================================================================
// Main CLI argument parsing
// ============================================================================

CLIApp::Config CLIApp::parse(int argc, char* argv[]) {
    std::string prog = argc > 0 ? argv[0] : "besq";

    auto cli_parser = cli::CLIParser(BESQ_OPTIONS, UserI18nTranslator{});
    auto result = cli_parser.parse(
        std::span<const char*>((const char**)argv, argc));

    if (!result.diagnostics.empty()) {
        Config early_cfg = bind(result);

        if (early_cfg.help) {
            return early_cfg;
        }
        if (early_cfg.version) {
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

    // `--profile` has a default_v ("builtin:vanilla") that CLIParser applies
    // during parse(), so the bound value cannot distinguish "omitted" from an
    // explicit `--profile builtin:vanilla`.  Scan argv for the literal token.
    cfg.profile_explicit = has_profile_token(argc, argv);

    // Handle --help / --version for clean parses (no diagnostics)
    if (cfg.help) {
        return cfg;
    }
    if (cfg.version) {
        return cfg;
    }

    // Post-bind: --memory (supports "auto")
    {
        auto& raw_mem = std::get<19>(result.value);
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
        auto& raw_cfg = std::get<17>(result.value);
        if (raw_cfg.has_value() && raw_cfg->empty())
            throw std::runtime_error(tr("cli.err.empty_config"));
    }

    // Post-bind: --algo-opt (empty + format check; values are arbitrary
    // strings, so only shape is validated — key ownership is strategy-defined)
    {
        auto& raw_opt = std::get<27>(result.value);
        if (raw_opt.has_value() && raw_opt->empty())
            throw std::runtime_error(tr("cli.err.empty_algo_opt"));
        if (!cfg.algo_opt_pairs.empty()) {
            for (const auto& pair : string_utils::split(cfg.algo_opt_pairs, ',')) {
                auto eq = pair.find('=');
                if (eq == std::string::npos || eq == 0 || eq + 1 >= pair.size())
                    throw std::runtime_error(tr_fmt("cli.err.invalid_algo_opt", pair));
            }
        }
    }

    // Business validation.  `--input` implies inventory mode, so an invalid
    // --mode must be rejected on that path too.
    if (!cfg.target.empty() || cfg.input.has_value()) {
        if (cfg.mode != "direct" && cfg.mode != "inventory")
            throw std::runtime_error(tr_fmt("cli.err.invalid_mode", cfg.mode));
    }
    if (cfg.platform != "java" && cfg.platform != "bedrock" && cfg.platform != "auto")
        throw std::runtime_error(tr_fmt("cli.err.invalid_platform", cfg.platform));
    if (cfg.format != "text" && cfg.format != "compact" && cfg.format != "json")
        throw std::runtime_error(tr_fmt("cli.err.invalid_format", cfg.format));
    if (cfg.solutions < 0)
        throw std::runtime_error(tr("cli.err.solutions_not_positive"));
    if (cfg.solutions > static_cast<int>(BESQ_MAX_SOLUTIONS))
        throw std::runtime_error(tr_fmt("cli.err.solutions_out_of_range", BESQ_MAX_SOLUTIONS));
    if (cfg.max_time.has_value() && *cfg.max_time < 0)
        throw std::runtime_error(tr("cli.err.max_time_negative"));

    // --source requires --target, EXCEPT when --input is present: inventory
    // mode rejects --source with its own (more specific) error in
    // build_solve_request, so the generic guard must not fire first.
    if (!cfg.source.empty() && cfg.target.empty() && !cfg.input.has_value())
        throw std::runtime_error(tr("cli.err.source_without_target"));

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
            if (k != "ignore-penalty-cost" && k != "ignore-repair-cost")
                throw std::runtime_error(tr_fmt("cli.err.unknown_config_key", k));
            if (v != "true" && v != "false")
                throw std::runtime_error(tr_fmt("cli.err.invalid_config_value", k, v));
        }
    }

    if (cfg.edit_ops.has_value()) {
        if (cfg.edit_ops->empty())
            throw std::runtime_error(tr("cli.err.empty_edit"));
        auto ops = string_utils::split(*cfg.edit_ops, ';');
        for (const auto& op : ops) {
            if (op.find(':') == std::string::npos)
                throw std::runtime_error(tr_fmt("cli.err.invalid_edit", op));
        }
    }

    if (cfg.algo_dir.has_value() && cfg.algo_dir->empty())
        throw std::runtime_error(tr_fmt("cli.err.empty_algo_dir"));
    if (cfg.export_path.has_value() && cfg.export_path->empty())
        throw std::runtime_error(tr_fmt("cli.err.empty_export"));
    if (cfg.resume.has_value() && cfg.resume->empty())
        throw std::runtime_error(tr("cli.err.empty_resume"));
    // --resume is a self-contained checkpoint: no target/source/input needed,
    // and mixing it with them is a contradiction (the checkpoint carries its
    // own AlgorithmInput).
    if (cfg.resume.has_value() && (!cfg.target.empty() || cfg.input.has_value()))
        throw std::runtime_error(tr("cli.err.resume_conflict"));

    if (!cfg.help && !cfg.version && !cfg.list_algorithms && !cfg.list_profiles && !cfg.list_langs) {
        if (cfg.target.empty() && !cfg.input.has_value()
            && !cfg.export_path.has_value()
            && !cfg.publish.has_value()
            && !cfg.resume.has_value()) {
            if (argc <= 1) {
                // Pure no-args: show brief usage + hint, then exit cleanly
                std::cout << tr_fmt("cli.help.usage", prog) << "\n";
                std::cout << tr_fmt("cli.help.usage_export", prog) << "\n";
                std::cout << tr_fmt("cli.err.try_help", prog) << "\n";
                flush_output();
                cfg.brief_usage = true;  // signal run() to skip further processing
            } else {
                throw std::runtime_error(tr("cli.err.missing_target_or_export"));
            }
        }
    }

    return cfg;
}

// ============================================================================
// apply_config_pairs — parse --config value and apply to ForgeConfig
// ============================================================================

void CLIApp::apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg) {
    if (config_pairs.empty()) return;
    auto pairs = string_utils::split(config_pairs, ',');
    for (const auto& pair : pairs) {
        auto eq = pair.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Invalid config pair: '" + pair + "'. Expected key=value format.\n");
        std::string k = pair.substr(0, eq);
        std::string v = pair.substr(eq + 1);
        bool val = (v == "true");
        if (k == "ignore-penalty-cost")  cfg.ignore_penalty_cost = val;
        else if (k == "ignore-repair-cost")  cfg.ignore_repair_cost = val;
    }
}

// apply_algo_opts — parse --algo-opt k=v,k=v into SearchConfig::extra.
// Values are arbitrary strings; keys are strategy-owned (namespaced by the
// strategy that reads them). Shape validated in parse(); here we only split.
void CLIApp::apply_algo_opts(const std::string& algo_opts, algorithm::SearchConfig& cfg) {
    if (algo_opts.empty()) return;
    for (const auto& pair : string_utils::split(algo_opts, ',')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= pair.size())
            throw std::runtime_error(tr_fmt("cli.err.invalid_algo_opt", pair));
        cfg.extra.emplace(pair.substr(0, eq), pair.substr(eq + 1));
    }
}

// ====================================================================
// build_solve_request — assemble a SolveRequest from a parsed Config
// ====================================================================
// Shared by CLIApp::run() and the CLI tests.  This is where CLI options
// are wired into algorithm::SearchConfig (max_search_time, memory_mb, ...).

SolveRequest CLIApp::build_solve_request(const Config& config, BesqContext& ctx) {
    const bool inventory = config.input.has_value() || config.mode == "inventory";

    SolveRequest request;
    request.mode = inventory ? AlgorithmMode::inventory : AlgorithmMode::direct;

    if (inventory) {
        if (!config.input)
            throw std::runtime_error(tr("cli.err.inventory_requires_input"));
        if (!config.source.empty())
            throw std::runtime_error(tr("cli.err.inventory_rejects_source"));

        // ── 两阶段：先结构解析（读 profile），激活 profile 后再交叉验证 ──
        std::string content = InventoryParser::read_content(*config.input);  // "-" → stdin
        auto dto = InventoryParser::parse_task(content);                     // structural (schema errors throw)
        // profile：CLI 显式 --profile 覆盖 JSON profile；否则 JSON 激活
        // （profile_explicit 由 argv token 判定，见 parse()；profile 字段本身
        //   携带 default_v="builtin:vanilla"，无法区分显式/缺省）
        if (!config.profile_explicit && !dto.profile.empty()) {
            auto profiles = ctx.list_profiles();
            if (std::find(profiles.begin(), profiles.end(), dto.profile) == profiles.end())
                throw std::runtime_error(tr_fmt("cli.err.profile_not_found", dto.profile));
            ctx.activate_profile(dto.profile);   // side effect: changes active profile
        }
        auto inv = InventoryParser::build_inventory(dto, ctx.enchantments(), ctx.equipment());

        // target：CLI --target 覆盖 JSON target；两者皆缺 → 报错
        if (!config.target.empty()) {
            request.target_item = ItemParser::parse(config.target, ctx.enchantments(), ctx.equipment());
        } else {
            request.target_item = inv.target_item;
            if (request.target_item.id.str().empty())
                throw std::runtime_error(tr("cli.err.inventory_requires_target"));
        }
        request.payload = InventoryPayload{std::move(inv.items), std::move(inv.priorities)};
        // algorithm：CLI 显式 > JSON > 默认 hamming
        request.algorithm = config.algorithm_explicit ? config.algorithm
            : (!inv.algorithm.empty() ? inv.algorithm : "hamming");
    } else {
        request.target_item = ItemParser::parse(config.target, ctx.enchantments(), ctx.equipment());
        request.payload = DirectPayload{};
        if (!config.source.empty())
            request.payload = DirectPayload{EnchParser::parse(config.source, ctx.enchantments())};
        request.algorithm = config.algorithm;
    }

    request.forge_config.platform = (config.platform == "bedrock")
        ? MCE::Bedrock : MCE::Java;
    request.search_config.max_solutions = config.solutions;
    request.search_config.max_threads = static_cast<uint32_t>(config.max_threads);
    // --max-time: only override when explicitly provided. 0 = unlimited
    // (strategies/executor treat 0 as "no timeout"). When omitted, the
    // SearchConfig default (180s) is left untouched.
    if (config.max_time.has_value())
        request.search_config.max_search_time = std::chrono::seconds(*config.max_time);
    // --memory: forward when set; 0 lets A* fall back to its own 2048 MB.
    request.search_config.memory_mb = config.memory_mb;
    CLIApp::apply_config_pairs(config.config_pairs, request.forge_config);
    CLIApp::apply_algo_opts(config.algo_opt_pairs, request.search_config);
    return request;
}

// ====================================================================
// apply_edits — parse --edit format and dispatch
// ====================================================================

void CLIApp::apply_edits(const std::string& ops, BesqContext& ctx) {
    auto op_list = string_utils::split(ops, ';');
    for (const auto& op : op_list) {
        if (op.empty())
            continue;

        auto parts = string_utils::split(op, ',');
        if (parts.size() < 2)
            throw std::runtime_error(tr_fmt("cli.err.invalid_edit_op", op));

        auto& header = parts[0];
        auto colon   = header.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error(tr_fmt("cli.err.invalid_edit_header", header));

        std::string target = header.substr(0, colon);
        std::string action = header.substr(colon + 1);
        std::string id     = parts[1];
        if (id.empty())
            throw std::runtime_error(tr_fmt("cli.err.empty_edit_id", op));

        if (action == "rm") {
            if (target == "ench") { ctx.remove_enchantment(id); continue; }
            if (target == "eq")   { ctx.remove_equipment(id);   continue; }
            throw std::runtime_error(tr_fmt("cli.err.unsupported_remove", target));
        }

        if (action == "add") {
            if (target == "cat") { ctx.add_category(id); continue; }

            // eq:add removed — an equipment added in isolation has no tag
            // membership (applicability is supported_items ∩ tags_of), so it
            // would be applicable to nothing. Equipment comes from data/profiles.

            if (target == "ench") {
                int32_t multiplier = 1, max_level = 1, limited_level = 0;
                bool is_treasure = false;
                for (size_t i = 2; i < parts.size(); ++i) {
                    auto eq_pos = parts[i].find('=');
                    if (eq_pos == std::string::npos) continue;
                    auto k = parts[i].substr(0, eq_pos);
                    auto v = parts[i].substr(eq_pos + 1);
                    try {
                        if (k == "multiplier")    multiplier = std::stoi(v);
                        if (k == "max_level")     max_level = std::stoi(v);
                        if (k == "limited_level") limited_level = std::stoi(v);
                    } catch (const std::exception&) {
                        throw std::runtime_error(tr_fmt("cli.err.invalid_numeric", k, v, op));
                    }
                    if (k == "is_treasure") is_treasure = (v == "true");
                }
                ctx.add_enchantment(EnchInfo{NSID(id), id, MCE::All, max_level, limited_level, multiplier, is_treasure, {}, {}});
                continue;
            }

            throw std::runtime_error(tr_fmt("cli.err.unknown_target", target));
        }

        if (action == "mod") {
            if (target == "ench") {
                EnchInfo patch;
                patch.max_level     = 0;
                patch.limited_level = -1;
                patch.multiplier    = 0;
                for (size_t i = 2; i < parts.size(); ++i) {
                    auto eq_pos = parts[i].find('=');
                    if (eq_pos == std::string::npos) continue;
                    auto k = parts[i].substr(0, eq_pos);
                    auto v = parts[i].substr(eq_pos + 1);
                    try {
                        if (k == "multiplier")    patch.multiplier = std::stoi(v);
                        if (k == "max_level")     patch.max_level = std::stoi(v);
                        if (k == "limited_level") patch.limited_level = std::stoi(v);
                    } catch (const std::exception&) {
                        throw std::runtime_error(tr_fmt("cli.err.invalid_numeric", k, v, op));
                    }
                }
                ctx.modify_enchantment(id, patch);
                continue;
            }

            throw std::runtime_error(tr_fmt("cli.err.unsupported_modify", target));
        }

        throw std::runtime_error(tr_fmt("cli.err.unknown_action", action));
    }
}
