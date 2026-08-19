#include "domain/interface/cli/CLIApp.h"
#include "AppConfig.h"
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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>

// ============================================================================
// CLIApp — Application runner
// ============================================================================

namespace {

/// 组合 profile key：逗号分隔 + trim + 忽略空段。单段 = 单 profile。
/// 定义在 run() 之前（run 的 profile selection 使用）。
std::vector<std::string> split_profile_members(const std::string& value) {
    std::vector<std::string> members;
    for (const auto& seg : string_utils::split(value, ',')) {
        std::string m = string_utils::trim(seg);
        if (!m.empty())
            members.push_back(std::move(m));
    }
    return members;
}

/// set_dir 持久化：read-modify-write config.json（保留未知字段；best-effort）。
void persist_dir_settings(bool profile, const std::string& dir) {
    Json o = AppConfig::load_config_file();
    if (!o.is_valid() || o.type() != JsonType::Object)
        o = Json::object();
    o[profile ? "profiles_dir" : "algo_dir"] = Json(dir);
    AppConfig::save_config_file(o);
}

/// 对齐列表格渲染（inspect text 形态）：表头 + 分隔行 + 行；列宽自适应。
/// 数值列右对齐（最后一列起，按列内容全部为整数字符串判定——简化：id/name 类左对齐，
/// 其余右对齐由调用方传 align 标记）。
void print_aligned_table(const std::vector<std::string>& headers,
                         const std::vector<std::vector<std::string>>& rows,
                         const std::vector<bool>& numeric) {
    const size_t cols = headers.size();
    std::vector<size_t> width(cols, 0);
    for (size_t c = 0; c < cols; ++c) width[c] = headers[c].size();
    for (const auto& row : rows)
        for (size_t c = 0; c < cols && c < row.size(); ++c)
            width[c] = std::max(width[c], row[c].size());
    auto line = [&](const std::vector<std::string>& cells) {
        std::string l;
        for (size_t c = 0; c < cols; ++c) {
            const std::string cell = c < cells.size() ? cells[c] : "";
            if (c > 0) l += "  ";
            if (numeric[c]) l += std::string(width[c] - cell.size(), ' ') + cell;
            else { l += cell; l += std::string(width[c] - cell.size(), ' '); }
        }
        return l;
    };
    std::cout << line(headers) << "\n";
    for (size_t c = 0; c < cols; ++c) { if (c > 0) std::cout << "  "; for (size_t i = 0; i < width[c]; ++i) std::cout << '-'; }
    std::cout << "\n";
    for (const auto& row : rows) std::cout << line(row) << "\n";
}

} // namespace

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

    // 2. --lang CLI flag override — 仅顶层：遇到命令名 token 即停止扫描
    //    （下游 --lang 不再识别；值恰为命令名的选项值可能误停——罕见，回退
    //    config.json / BESQ_LANG）。
    static constexpr std::string_view kSubcommands[] = {
        "solve", "calc", "profile", "algo", "algorithm", "serve",
    };
    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        for (auto c : kSubcommands)
            if (a == c)
                return;   // 子命令已出现：其后的 --lang 不识别
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

    // CLI console logging: OFF by default (silent — diagnostics go to the
    // log files only); --verbose turns it on.  An explicit BESQ_LOG_CONSOLE
    // env wins over the default.  For machine formats (json/compact) verbose
    // only opens Warn+ (stderr) so stdout stays parseable; text mode opens
    // Debug+.
    {
        auto& cfg = AppConfig::get();
        if (!config.verbose && get_env_str("BESQ_LOG_CONSOLE").empty())
            cfg.log_console = false;
        if (config.verbose) {
            cfg.log_console = true;
            cfg.log_console_level = (config.format == "text") ? 0 : 2;
        }
        setup_logger(cfg.logger_config());
    }

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

    // ── 子命令分派 ──
    switch (config.cmd) {
        case Config::Cmd::profile: return run_profile(config);
        case Config::Cmd::algo:    return run_algo(config);
        default: break;   // solve
    }

    // 1b. --list-langs: language-only listing — NO domain auto-load (which
    //     would parse profiles/plugins and surface unrelated warnings, e.g.
    //     a broken datapack in the profiles dir).  Only the on-disk langs
    //     directory (AppConfig.langs_dir) is pre-registered (missing → silent).
    if (config.list_langs) {
        const std::string langs_dir = AppConfig::get().langs_dir;
        if (!langs_dir.empty()) {
            LanguageManager::instance().set_langs_dir(langs_dir);
            LanguageManager::instance().load_all_from_disk();
        }
        auto langs = LanguageManager::instance().available();
        std::cout << tr_fmt("cli.msg.list_langs", langs.size()) << "\n";
        for (const auto& l : langs)
            std::cout << "  " << l << "\n";
        flush_output();
        return 0;
    }

    // 2. Domain-wide auto-load (built-in → profiles → algorithms → langs).
    //    The info-only flags that used to skip it (--list-profiles /
    //    --list-algorithms) moved to the profile/algo subcommands (Task 5),
    //    so the solve domain always auto-loads.
    _ctx.auto_load();

    // Algorithm name early validation (U10)
    if (!config.algorithm.empty() && !config.help && !config.version) {
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

    // 3. Profile selection
    //    auto_load() (step 2) already scanned `<exe_dir>/profiles` (safe
    //    no-op when missing).  activate_profile must run before any
    //    inventory solve so the task's enchantments resolve correctly.
    //    Profile 目录：持久化 profiles_dir（set_dir / BESQ_PROFILES_DIR）> 默认
    {
        const std::string pdir = AppConfig::get().profiles_dir;
        if (!pdir.empty())
            _ctx.set_profiles_dir(pdir);
    }
    _ctx.load_profiles();
    if (config.profile) {
        auto members = split_profile_members(*config.profile);
        if (members.size() > 1)
            _ctx.activate_profile_group(std::move(members));
        else if (members.size() == 1)
            _ctx.activate_profile(members.front());
        // 空组合 → 保持默认（builtin:vanilla）
    }

    // 4. Resume from checkpoint (self-contained: no target/source needed)
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

    // 5. Solve
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
// profile / algo 子命令处理器
// ============================================================================

int CLIApp::run_profile(const Config& config) {
    // set_dir 持久化的目录优先于默认
    const std::string pdir = AppConfig::get().profiles_dir;
    if (!pdir.empty())
        _ctx.set_profiles_dir(pdir);

    switch (config.profile_action) {
        case Config::ProfileAction::list: {
            _ctx.load_profiles();
            auto profiles = _ctx.list_profiles();
            std::cout << tr_fmt("cli.msg.list_profiles", profiles.size()) << "\n";
            for (const auto& p : profiles)
                std::cout << "  " << p << "\n";
            flush_output();
            return 0;
        }
        case Config::ProfileAction::set_dir: {
            if (config.profile_target.empty())
                throw std::runtime_error(tr("cli.err.empty_dir"));
            persist_dir_settings(true, config.profile_target);
            _ctx.set_profiles_dir(config.profile_target);
            std::cout << tr_fmt("cli.msg.dir_set", config.profile_target) << "\n";
            flush_output();
            return 0;
        }
        case Config::ProfileAction::import: {
            if (_ctx.composite_active())
                throw std::runtime_error(tr("cli.err.composite_readonly"));
            _ctx.load_profiles();
            _ctx.import_profile(config.profile_target);
            flush_output();
            return 0;
        }
        case Config::ProfileAction::export_: {
            if (config.export_format != "json" && config.export_format != "csv")
                throw std::runtime_error(tr_fmt("cli.err.invalid_value", config.export_format));
            _ctx.load_profiles();
            if (!config.export_profile.empty()) {
                if (!_ctx.profile_exists(config.export_profile))
                    throw std::runtime_error(tr_fmt("cli.err.profile_not_found", config.export_profile));
                _ctx.activate_profile(config.export_profile);
            }
            if (config.export_format == "json") {
                std::string json = _ctx.export_profile_to_string();
                if (config.export_file.empty() || config.export_file == "-") {
                    std::cout << json << "\n";
                    flush_output();
                } else {
                    std::ofstream out(config.export_file);
                    if (!out) throw std::runtime_error(
                        tr_fmt("main.err.export_failed", config.export_file));
                    out << json;
                    flush_output();
                }
            } else {   // csv：extension-driven（现有 export_profile）
                if (config.export_file.empty() || config.export_file == "-")
                    throw std::runtime_error(tr_fmt("cli.err.invalid_value", "-"));
                bool ok = _ctx.export_profile(config.export_file);
                if (!ok) throw std::runtime_error(
                    tr_fmt("main.err.export_failed", config.export_file));
                flush_output();
            }
            return 0;
        }
        case Config::ProfileAction::info: {
            _ctx.load_profiles();
            if (!_ctx.profile_exists(config.profile_target))
                throw std::runtime_error(tr_fmt("cli.err.profile_not_found", config.profile_target));
            ProfileMeta meta = _ctx.profile_metadata(config.profile_target);
            std::cout << meta.name << "\n";
            if (!meta.description.empty()) std::cout << "  " << meta.description << "\n";
            std::cout << "  version: " << (meta.version.empty() ? std::string("-") : meta.version) << "\n";
            std::cout << "  mc_version: " << (meta.mc_version.empty() ? std::string("-") : meta.mc_version) << "\n";
            if (!meta.release_tag.empty()) std::cout << "  tag: " << meta.release_tag << "\n";
            std::cout << "  enchantments: " << meta.ench_count
                      << "  equipment: " << meta.eq_count
                      << "  tags: " << meta.tag_count << "\n";
            flush_output();
            return 0;
        }
        case Config::ProfileAction::publish: {
            if (config.profile_target.empty())
                throw std::runtime_error(tr_fmt("cli.err.profile_not_found", ""));
            std::string version = config.publish_version.empty() ? "dev" : config.publish_version;
            std::string tag     = config.publish_tag;
            std::string out     = config.profile_target + ".json";
            bool ok = _ctx.publish_profile(config.profile_target, version, tag, out);
            if (!ok)
                throw std::runtime_error(tr_fmt("main.err.publish_failed", config.profile_target));
            LOG_INFO("%s", tr_fmt("main.msg.published", out).c_str());
            flush_output();
            return 0;
        }
        case Config::ProfileAction::inspect: {
            if (config.inspect_format != "text" && config.inspect_format != "json")
                throw std::runtime_error(tr_fmt("cli.err.invalid_value", config.inspect_format));
            // kind 规范化（Global Constraint c）
            std::string kind;
            {
                std::string k = config.inspect_kind;
                for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (k == "equip" || k == "equipments" || k == "equipment") kind = "equip";
                else if (k == "ench" || k == "enchantments" || k == "enchantment") kind = "ench";
                else if (k == "tags") kind = "tags";
                else throw std::runtime_error(tr_fmt("cli.err.invalid_inspect_kind", config.inspect_kind));
            }
            _ctx.load_profiles();
            if (!_ctx.profile_exists(config.profile_target))
                throw std::runtime_error(tr_fmt("cli.err.profile_not_found", config.profile_target));
            const Profile& p = _ctx.profile(config.profile_target);
            // ── 收集行（filter → fields → 分页；count = 过滤后分页前）──
            std::vector<std::string> headers;
            std::vector<std::vector<std::string>> rows;
            std::vector<bool> numeric;
            auto contains = [](const std::string& hay, const std::string& needle) {
                if (needle.empty()) return true;
                auto h = hay; auto n = needle;
                for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return h.find(n) != std::string::npos;
            };
            auto pick_fields = [&](const std::vector<std::string>& all,
                                   const std::vector<std::string>& all_headers,
                                   const std::vector<bool>& all_numeric) {
                // fields 空 → 全列；否则按逗号选择（未知列报错）
                std::vector<std::string> want;
                if (config.inspect_fields.empty()) want = all_headers;
                else for (const auto& f : string_utils::split(config.inspect_fields, ','))
                    want.push_back(string_utils::trim(f));
                for (const auto& w : want)
                    if (std::find(all_headers.begin(), all_headers.end(), w) == all_headers.end())
                        throw std::runtime_error(tr_fmt("cli.err.invalid_inspect_field", w,
                            string_utils::join(all_headers, ", ")));
                std::vector<std::string> hs, cells;
                std::vector<bool> nums;
                for (size_t i = 0; i < all_headers.size(); ++i)
                    if (std::find(want.begin(), want.end(), all_headers[i]) != want.end()) {
                        hs.push_back(all_headers[i]); cells.push_back(all[i]); nums.push_back(all_numeric[i]);
                    }
                return std::make_tuple(hs, cells, nums);
            };
            auto str = [](const NSID& id) { return id.str(); };
            if (kind == "ench") {
                headers = {"id", "name", "max_level", "limited_level", "multiplier", "is_treasure"};
                numeric = {false, false, true, true, true, false};
                for (const auto& e : p.ench()) {
                    if (!contains(e.id.str(), config.inspect_filter)
                        && !contains(e.name, config.inspect_filter)) continue;
                    std::vector<std::string> all = {str(e.id), e.name,
                        std::to_string(e.max_level), std::to_string(e.limited_level),
                        std::to_string(e.multiplier), e.is_treasure ? "true" : "false"};
                    auto [hs, cells, nums] = pick_fields(all, headers, numeric);
                    rows.push_back(cells); headers = hs; numeric = nums;
                }
            } else if (kind == "equip") {
                headers = {"id", "name", "category", "max_durability"};
                numeric = {false, false, false, true};
                for (const auto& e : p.eq()) {
                    if (!contains(e.id.str(), config.inspect_filter)
                        && !contains(e.name, config.inspect_filter)) continue;
                    std::vector<std::string> all = {str(e.id), e.name, str(e.category),
                        std::to_string(e.max_durability)};
                    auto [hs, cells, nums] = pick_fields(all, headers, numeric);
                    rows.push_back(cells); headers = hs; numeric = nums;
                }
            } else {
                headers = {"id", "name"};
                numeric = {false, false};
                for (const auto& t : p.tags()) {
                    if (!contains(t.id.str(), config.inspect_filter)) continue;
                    std::vector<std::string> all = {str(t.id), t.name};
                    auto [hs, cells, nums] = pick_fields(all, headers, numeric);
                    rows.push_back(cells); headers = hs; numeric = nums;
                }
            }
            const size_t total = rows.size();
            if (config.inspect_limit > 0) {
                const size_t page = config.inspect_page > 0 ? static_cast<size_t>(config.inspect_page) : 1;
                const size_t start = (page - 1) * static_cast<size_t>(config.inspect_limit);
                std::vector<std::vector<std::string>> page_rows;
                for (size_t i = start; i < rows.size() && page_rows.size() < static_cast<size_t>(config.inspect_limit); ++i)
                    page_rows.push_back(std::move(rows[i]));
                rows = std::move(page_rows);
            }
            if (config.inspect_format == "json") {
                Json o = Json::object();
                o["kind"] = Json(kind);
                o["count"] = Json(static_cast<int64_t>(total));
                Json arr = Json::array();
                for (const auto& row : rows) {
                    Json r = Json::object();
                    for (size_t c = 0; c < headers.size() && c < row.size(); ++c)
                        r[headers[c]] = numeric[c] ? Json(static_cast<int64_t>(std::stoll(row[c]))) : Json(row[c]);
                    arr.push_back(std::move(r));
                }
                o["rows"] = std::move(arr);
                std::cout << o.to_string() << "\n";
            } else {
                std::cout << tr_fmt("cli.msg.inspect_count", rows.size(), total) << "\n";
                print_aligned_table(headers, rows, numeric);
            }
            flush_output();
            return 0;
        }
        default:
            // 无动作（如 `besq profile` 裸命令）不再静默成功——按解析失败处理
            throw std::runtime_error(tr("cli.err.parse_failed"));
    }
}

int CLIApp::run_algo(const Config& config) {
    switch (config.algo_action) {
        case Config::AlgoAction::list: {
            const std::string algo_dir = AppConfig::get().algo_dir;
            if (std::filesystem::is_directory(algo_dir))
                _ctx.load_algorithms(algo_dir);
            auto algos = _ctx.list_algorithms();
            std::cout << tr_fmt("cli.msg.list_algorithms", algos.size()) << "\n";
            for (const auto& name : algos)
                std::cout << "  " << name << "\n";
            flush_output();
            return 0;
        }
        case Config::AlgoAction::set_dir: {
            if (config.algo_target.empty())
                throw std::runtime_error(tr("cli.err.empty_dir"));
            AppConfig::get().algo_dir = config.algo_target;
            persist_dir_settings(false, config.algo_target);
            std::cout << tr_fmt("cli.msg.dir_set", config.algo_target) << "\n";
            flush_output();
            return 0;
        }
        default:
            // 无动作（如 `besq algo` 裸命令）不再静默成功——按解析失败处理
            throw std::runtime_error(tr("cli.err.parse_failed"));
    }
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

// ── serve 表 ──
const auto SERVE_OPTS = OptionTable{
    Flag{.long_name = "help",    .short_name = 'h', .help_key = "cli.help.help_desc",    .help_group = "cli.help.group_info"},
    Flag{.long_name = "verbose", .short_name = 'v', .help_key = "cli.help.verbose_desc", .help_group = "cli.help.group_output"},
    Flag{.long_name = "version", .short_name = 'V', .help_key = "cli.help.version_desc", .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "host",    .help_key = "cli.help.host_desc",    .help_group = "cli.help.group_platform"},
    Option<unsigned int>{.long_name = "port",    .help_key = "cli.help.port_desc",    .help_group = "cli.help.group_platform", .default_v = 0u},
    Option<unsigned int>{.long_name = "workers", .help_key = "cli.help.workers_desc", .help_group = "cli.help.group_platform"},
    Option<std::string>{.long_name = "res-dir", .help_key = "cli.help.res_dir_desc", .help_group = "cli.help.group_platform"},
};

// ── algo 表 ──
const auto ALGO_OPTS = OptionTable{
    Flag{.long_name = "help",    .short_name = 'h', .help_key = "cli.help.help_desc",    .help_group = "cli.help.group_info"},
    Flag{.long_name = "verbose", .short_name = 'v', .help_key = "cli.help.verbose_desc", .help_group = "cli.help.group_output"},
    Flag{.long_name = "version", .short_name = 'V', .help_key = "cli.help.version_desc", .help_group = "cli.help.group_info"},
    Command<>{.name = "list", .help_key = "cli.cmd.list_desc"},
    Command<Positional<std::string>>{.name = "set_dir", .help_key = "cli.cmd.set_dir_desc",
        .table = OptionTable{Positional<std::string>{.name = "dir", .help_key = "cli.help.dir_desc"}}},
};

// ── profile 表 ──
const auto PROFILE_OPTS = OptionTable{
    Flag{.long_name = "help",    .short_name = 'h', .help_key = "cli.help.help_desc",    .help_group = "cli.help.group_info"},
    Flag{.long_name = "verbose", .short_name = 'v', .help_key = "cli.help.verbose_desc", .help_group = "cli.help.group_output"},
    Flag{.long_name = "version", .short_name = 'V', .help_key = "cli.help.version_desc", .help_group = "cli.help.group_info"},
    Command<>{.name = "list", .help_key = "cli.cmd.list_desc"},
    Command<Positional<std::string>>{.name = "set_dir", .help_key = "cli.cmd.set_dir_desc",
        .table = OptionTable{Positional<std::string>{.name = "dir", .help_key = "cli.help.dir_desc"}}},
    Command<Positional<std::string>>{.name = "import", .help_key = "cli.cmd.import_desc",
        .table = OptionTable{Positional<std::string>{.name = "file", .help_key = "cli.help.file_desc"}}},
    Command<Option<std::string>, Option<std::string>, Option<std::string>>{
        .name = "export", .help_key = "cli.cmd.export_desc",
        .table = OptionTable{
            Option<std::string>{.long_name = "profile", .help_key = "cli.help.profile_desc"},
            Option<std::string>{.long_name = "file",    .help_key = "cli.help.file_desc"},
            Option<std::string>{.long_name = "format",  .help_key = "cli.help.export_format_desc", .default_v = std::string("json")},
        }},
    Command<Positional<std::string>>{.name = "info", .help_key = "cli.cmd.info_desc",
        .table = OptionTable{Positional<std::string>{.name = "profile", .help_key = "cli.help.profile_desc"}}},
    Command<Positional<std::string>, Option<std::string>, Option<std::string>>{
        .name = "publish", .help_key = "cli.cmd.publish_desc",
        .table = OptionTable{
            Positional<std::string>{.name = "profile", .help_key = "cli.help.profile_desc"},
            Option<std::string>{.long_name = "version", .help_key = "cli.help.publish_version_desc"},
            Option<std::string>{.long_name = "tag",     .help_key = "cli.help.publish_tag_desc"},
        }},
    Command<Positional<std::string>, Positional<std::string>,
            Option<std::string>, Option<std::string>, Option<int>, Option<int>, Option<std::string>>{
        .name = "inspect", .help_key = "cli.cmd.inspect_desc",
        .table = OptionTable{
            Positional<std::string>{.name = "profile", .help_key = "cli.help.profile_desc"},
            Positional<std::string>{.name = "kind",    .help_key = "cli.cmd.inspect_kind_desc"},
            Option<std::string>{.long_name = "filter", .help_key = "cli.help.filter_desc"},
            Option<std::string>{.long_name = "fields", .help_key = "cli.help.fields_desc"},
            Option<int>{.long_name = "limit", .help_key = "cli.help.limit_desc", .default_v = 0},
            Option<int>{.long_name = "page",  .help_key = "cli.help.page_desc",  .default_v = 1},
            Option<std::string>{.long_name = "format", .help_key = "cli.help.format_desc", .default_v = std::string("text")},
        }},
};

// ── solve 命令表 = 顶层表的前 20 项（全局三项 + list-langs + lang + 16 solve 选项）──
const auto SOLVE_CMD_OPTS = OptionTable{
    Flag{.long_name = "help",    .short_name = 'h', .help_key = "cli.help.help_desc",    .help_group = "cli.help.group_info"},
    Flag{.long_name = "verbose", .short_name = 'v', .help_key = "cli.help.verbose_desc", .help_group = "cli.help.group_output"},
    Flag{.long_name = "version", .short_name = 'V', .help_key = "cli.help.version_desc", .help_group = "cli.help.group_info"},
    Flag{.long_name = "list-langs", .help_key = "cli.help.list_langs_desc", .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "lang", .help_key = "cli.help.lang_desc", .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "algorithm", .alt_long = "algo", .help_key = "cli.help.algorithm_desc", .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "target", .short_name = 't', .help_key = "cli.help.target_desc", .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "source", .short_name = 's', .help_key = "cli.help.source_desc", .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "platform", .alt_long = "mce", .help_key = "cli.help.platform_desc", .help_group = "cli.help.group_platform", .default_v = std::string("auto")},
    Option<std::string>{.long_name = "format", .short_name = 'f', .help_key = "cli.help.format_desc", .help_group = "cli.help.group_output", .default_v = std::string("text")},
    Option<int>{.long_name = "solutions", .short_name = 'n', .help_key = "cli.help.solutions_desc", .help_group = "cli.help.group_basic", .default_v = 1},
    Option<std::string>{.long_name = "memory", .help_key = "cli.help.memory_desc", .help_group = "cli.help.group_advanced"},
    Option<int>{.long_name = "max-time", .help_key = "cli.help.max_time_desc", .help_group = "cli.help.group_advanced"},
    Option<int>{.long_name = "max-threads", .short_name = 'j', .help_key = "cli.help.max_threads_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "algo-opt", .help_key = "cli.help.algo_opt_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "input", .short_name = 'i', .help_key = "cli.help.input_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "output", .short_name = 'o', .help_key = "cli.help.output_desc", .help_group = "cli.help.group_output"},
    Option<std::string>{.long_name = "resume", .help_key = "cli.help.resume_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "config", .short_name = 'c', .help_key = "cli.help.config_desc", .help_group = "cli.help.group_platform"},
    Option<std::string>{.long_name = "profile", .help_key = "cli.help.profile_desc", .help_group = "cli.help.group_profile", .default_v = std::string("builtin:vanilla")},
};

// ── 顶层表（= solve，默认）：SOLVE_CMD_OPTS 同布局 + 4 个命令条目 ──
const auto BESQ_OPTIONS = OptionTable{
    Flag{.long_name = "help",    .short_name = 'h', .help_key = "cli.help.help_desc",    .help_group = "cli.help.group_info"},
    Flag{.long_name = "verbose", .short_name = 'v', .help_key = "cli.help.verbose_desc", .help_group = "cli.help.group_output"},
    Flag{.long_name = "version", .short_name = 'V', .help_key = "cli.help.version_desc", .help_group = "cli.help.group_info"},
    Flag{.long_name = "list-langs", .help_key = "cli.help.list_langs_desc", .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "lang", .help_key = "cli.help.lang_desc", .help_group = "cli.help.group_info"},
    Option<std::string>{.long_name = "algorithm", .alt_long = "algo", .help_key = "cli.help.algorithm_desc", .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "target", .short_name = 't', .help_key = "cli.help.target_desc", .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "source", .short_name = 's', .help_key = "cli.help.source_desc", .help_group = "cli.help.group_basic"},
    Option<std::string>{.long_name = "platform", .alt_long = "mce", .help_key = "cli.help.platform_desc", .help_group = "cli.help.group_platform", .default_v = std::string("auto")},
    Option<std::string>{.long_name = "format", .short_name = 'f', .help_key = "cli.help.format_desc", .help_group = "cli.help.group_output", .default_v = std::string("text")},
    Option<int>{.long_name = "solutions", .short_name = 'n', .help_key = "cli.help.solutions_desc", .help_group = "cli.help.group_basic", .default_v = 1},
    Option<std::string>{.long_name = "memory", .help_key = "cli.help.memory_desc", .help_group = "cli.help.group_advanced"},
    Option<int>{.long_name = "max-time", .help_key = "cli.help.max_time_desc", .help_group = "cli.help.group_advanced"},
    Option<int>{.long_name = "max-threads", .short_name = 'j', .help_key = "cli.help.max_threads_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "algo-opt", .help_key = "cli.help.algo_opt_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "input", .short_name = 'i', .help_key = "cli.help.input_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "output", .short_name = 'o', .help_key = "cli.help.output_desc", .help_group = "cli.help.group_output"},
    Option<std::string>{.long_name = "resume", .help_key = "cli.help.resume_desc", .help_group = "cli.help.group_advanced"},
    Option<std::string>{.long_name = "config", .short_name = 'c', .help_key = "cli.help.config_desc", .help_group = "cli.help.group_platform"},
    Option<std::string>{.long_name = "profile", .help_key = "cli.help.profile_desc", .help_group = "cli.help.group_profile", .default_v = std::string("builtin:vanilla")},
    // ── 命令（索引 20-23）──
    Command<Flag, Flag, Flag, Flag, Option<std::string>, Option<std::string>, Option<std::string>, Option<std::string>, Option<std::string>, Option<std::string>, Option<int>, Option<std::string>, Option<int>, Option<int>, Option<std::string>, Option<std::string>, Option<std::string>, Option<std::string>, Option<std::string>, Option<std::string>>{
        .name = "solve", .alias = "calc", .help_key = "cli.cmd.solve_desc",
        .table = SOLVE_CMD_OPTS,
    },
    Command<Flag, Flag, Flag, Command<>, Command<Positional<std::string>>, Command<Positional<std::string>>, Command<Option<std::string>, Option<std::string>, Option<std::string>>, Command<Positional<std::string>>, Command<Positional<std::string>, Option<std::string>, Option<std::string>>, Command<Positional<std::string>, Positional<std::string>, Option<std::string>, Option<std::string>, Option<int>, Option<int>, Option<std::string>>>{
        .name = "profile", .help_key = "cli.cmd.profile_desc", .table = PROFILE_OPTS,
    },
    Command<Flag, Flag, Flag, Command<>, Command<Positional<std::string>>>{
        .name = "algo", .alias = "algorithm", .help_key = "cli.cmd.algo_desc", .table = ALGO_OPTS,
    },
    Command<Flag, Flag, Flag, Option<std::string>, Option<unsigned int>, Option<unsigned int>, Option<std::string>>{
        .name = "serve", .help_key = "cli.cmd.serve_desc", .table = SERVE_OPTS,
    },
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
            case unknown_command:
                return tr_fmt("cli.err.unknown_command", diag.arg);
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

    // Examples（顶层帮助专属）
    r += "\nExamples:\n";
    r += "  " + std::string(program_name) + " --target diamond_sword[sharpness=5,knockback=2]\n";
    r += "  " + std::string(program_name) + " --source \"protection=4\" --target diamond_chestplate[protection=5,unbreaking=3]\n";
    r += "  " + std::string(program_name) + " profile export --file out.json\n\n";

    // Enchantment format reference
    r += tr("cli.help.ench_format_header") + "\n";
    r += "  " + tr("cli.help.ench_format_id_level") + "\n";
    r += "  " + tr("cli.help.ench_format_nsid_level") + "\n";
    r += "  " + tr("cli.help.ench_format_colon") + "\n";

    return r;
}

std::string CLIApp::help_text(std::string_view program_name,
                              std::span<const std::string_view> command_path) {
    if (command_path.empty())
        return help_text(program_name);
    return cli::CLIParser(BESQ_OPTIONS, UserI18nTranslator{}).format_help(program_name, command_path);
}

// ============================================================================
// Main CLI argument parsing
// ============================================================================

namespace {

/// solve 域 post-bind：--memory 解析、--config/--algo-opt 形状、业务校验、门禁。
/// 顶层与 solve 命令共用（两表索引 0-19 布局一致）。
template<typename... Entries>
void post_bind_solve(CLIApp::Config& cfg, const cli::ParseResult<Entries...>& result,
                     int argc, char* argv[], const std::string& prog) {
    // --memory（支持 "auto"）——原索引 19 → 11
    {
        auto& raw_mem = std::get<11>(result.value);
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
    // --config 空值检查——原索引 17 → 18
    {
        auto& raw_cfg = std::get<18>(result.value);
        if (raw_cfg.has_value() && raw_cfg->empty())
            throw std::runtime_error(tr("cli.err.empty_config"));
    }
    // --algo-opt 形状检查——原索引 27 → 14
    {
        auto& raw_opt = std::get<14>(result.value);
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
    // ── 业务校验（solve 域）──
    // mode 现在由 --source 推断（build_solve_request），此处保留防御性不变式
    //（切片 1 恒不触发；JSON 任务 schema 无 mode 字段——用户确认 invalid_mode 路径保留）。
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
    // --source requires --target（input 模式在 build_solve_request 报更具体错误）
    if (!cfg.source.empty() && cfg.target.empty() && !cfg.input.has_value())
        throw std::runtime_error(tr("cli.err.source_without_target"));
    // --config 对校验
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
    // --resume 自包含检查（原逻辑保留）
    if (cfg.resume.has_value() && cfg.resume->empty())
        throw std::runtime_error(tr("cli.err.empty_resume"));
    if (cfg.resume.has_value() && (!cfg.target.empty() || cfg.input.has_value()))
        throw std::runtime_error(tr("cli.err.resume_conflict"));
    // ── 门禁：无 target/input/resume（且非 info 类）→ 报错或 brief usage ──
    //（export/publish/list 已迁出 solve 域，不再参与门禁；brief_usage 的 flush
    //  由 run() 出口/析构统一覆盖——flush_output 是私有静态，此处不可访问）
    if (!cfg.help && !cfg.version && !cfg.list_langs) {
        if (cfg.target.empty() && !cfg.input.has_value() && !cfg.resume.has_value()) {
            if (argc <= 1) {
                std::cout << tr_fmt("cli.help.usage", prog) << "\n";
                std::cout << tr_fmt("cli.err.try_help", prog) << "\n";
                cfg.brief_usage = true;
            } else {
                throw std::runtime_error(tr("cli.err.missing_target"));
            }
        }
    }
}

/// 展平本层 + 所有嵌套命令层的诊断。命令子表的错误（如 `profile list --lang`）
/// 只落在嵌套 ParseResult 里，顶层 diagnostics 为空——错误路径必须递归收集。
template<typename... Entries>
void collect_diagnostics(const cli::ParseResult<Entries...>& r, std::vector<cli::Diagnostic>& out) {
    out.insert(out.end(), r.diagnostics.begin(), r.diagnostics.end());
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&] {
            using ET = std::tuple_element_t<Is, std::tuple<Entries...>>;
            if constexpr (cli::is_command<ET>::value) {
                const auto& v = std::get<Is>(r.value);
                if (v.has_value()) collect_diagnostics(*v, out);
            }
        }()), ...);
    }(std::index_sequence_for<Entries...>{});
}

} // namespace

CLIApp::Config CLIApp::parse(int argc, char* argv[]) {
    std::string prog = argc > 0 ? argv[0] : "besq";

    auto cli_parser = cli::CLIParser(BESQ_OPTIONS, UserI18nTranslator{});
    auto result = cli_parser.parse(std::span<const char*>((const char**)argv, argc));

    // ── 分派（顶层无命令 = solve）──
    auto dispatch = [&]() -> Config {
        if (result.command_path.empty())
            return bind_solve_result(result);
        const std::string_view c0 = result.command_path.front();
        Config cfg;
        if (c0 == "solve")        cfg = bind_solve_result(*std::get<20>(result.value));
        else if (c0 == "profile") cfg = bind_profile_result(*std::get<21>(result.value));
        else if (c0 == "algo")    cfg = bind_algo_result(*std::get<22>(result.value));
        else if (c0 == "serve")   cfg = bind_serve_result(*std::get<23>(result.value));
        else                      return bind_solve_result(result);   // 不可达（命令名校验保证）
        // 命令前全局旗标转发：help/verbose/version 三旗标已复制进每张子表，顶层
        // 结果同样持有（索引 0/1/2）。仅在置位时转发——子结果自身旗标保持权威。
        const auto& top = result.value;
        if (std::get<0>(top)) cfg.help    = true;
        if (std::get<1>(top)) cfg.verbose = true;
        if (std::get<2>(top)) cfg.version = true;
        return cfg;
    };

    if (!result.ok()) {
        Config early_cfg = dispatch();
        if (early_cfg.help)  return early_cfg;
        if (early_cfg.version) return early_cfg;
        for (auto& msg : result.all_messages())
            std::cerr << msg << std::endl;
        std::vector<cli::Diagnostic> diags;
        collect_diagnostics(result, diags);
        for (auto& d : diags) {
            switch (d.code) {
                case cli::ParseErrorCode::required_missing:
                case cli::ParseErrorCode::unknown_option:
                case cli::ParseErrorCode::invalid_value:
                case cli::ParseErrorCode::missing_value:
                case cli::ParseErrorCode::unknown_command:
                    throw std::runtime_error(tr("cli.err.parse_failed"));
                default: break;
            }
        }
    }

    Config cfg = dispatch();
    cfg.command_path = result.command_path;
    if (cfg.help || cfg.version)
        return cfg;

    if (cfg.cmd == Config::Cmd::serve && cfg.serve_port > 65535)
        throw std::runtime_error(tr_fmt("cli.err.invalid_value", std::to_string(cfg.serve_port)));

    if (cfg.cmd == Config::Cmd::solve) {
        // `--profile` 有 default_v，无法区分显式/缺省——token 扫描判定
        cfg.profile_explicit = has_profile_token(argc, argv);
        if (result.command_path.empty())
            post_bind_solve(cfg, result, argc, argv, prog);
        else
            post_bind_solve(cfg, *std::get<20>(result.value), argc, argv, prog);
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
    // ── mode 判定：--input 自包含任务 → inventory；否则 --source 值类型推断：
    //    整个值可解析为附魔列表 → direct；解析失败 → 按物品列表 → inventory。
    bool inventory = config.input.has_value();
    if (!inventory && !config.source.empty()) {
        try {
            EnchParser::parse(config.source, ctx.enchantments());
        } catch (const std::exception&) {
            inventory = true;
        }
    }

    SolveRequest request;
    request.mode = inventory ? AlgorithmMode::inventory : AlgorithmMode::direct;

    if (inventory) {
        if (config.input) {
            if (!config.source.empty())
                throw std::runtime_error(tr("cli.err.inventory_rejects_source"));
            // ── 两阶段：先结构解析（读 profile），激活 profile 后再交叉验证 ──
            std::string content = InventoryParser::read_content(*config.input);  // "-" → stdin
            auto dto = InventoryParser::parse_task(content);                     // structural (schema errors throw)
            // profile：CLI 显式 --profile 覆盖 JSON profile；否则 JSON 激活
            // （profile_explicit 由 argv token 判定，见 parse()；profile 字段本身
            //   携带 default_v="builtin:vanilla"，无法区分显式/缺省）
            if (!config.profile_explicit && !dto.profile.empty()) {
                auto members = split_profile_members(dto.profile);
                for (const auto& m : members) {
                    auto profiles = ctx.list_profiles();
                    if (std::find(profiles.begin(), profiles.end(), m) == profiles.end())
                        throw std::runtime_error(tr_fmt("cli.err.profile_not_found", m));
                }
                if (members.size() > 1)
                    ctx.activate_profile_group(std::move(members));
                else if (members.size() == 1)
                    ctx.activate_profile(members.front());
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
            // ── --source 物品列表（逗号分隔 item 或 item[ench]）→ inventory ──
            std::vector<Item> items;
            for (const auto& seg : string_utils::split(config.source, ',')) {
                if (seg.empty()) continue;
                try {
                    items.push_back(ItemParser::parse(seg, ctx.enchantments(), ctx.equipment()));
                } catch (const std::exception&) {
                    // 混排/非法段：报两种接受形式，而非裸 ItemParser 错误
                    throw std::runtime_error(tr_fmt("cli.err.invalid_source_forms", config.source));
                }
            }
            if (items.empty())
                throw std::runtime_error(tr_fmt("cli.err.invalid_value", config.source));
            request.target_item = ItemParser::parse(config.target, ctx.enchantments(), ctx.equipment());
            request.payload = InventoryPayload{std::move(items), {}};
            // 物品列表 inventory：显式 --algorithm 优先，否则默认 hamming
            //（与 --input JSON 路径一致；dp_merge 为 direct-only）
            request.algorithm = config.algorithm_explicit ? config.algorithm : "hamming";
        }
        // 防御性不变式（invalid_mode 路径保留；切片 1 恒不触发）
        if (request.mode != AlgorithmMode::direct && request.mode != AlgorithmMode::inventory)
            throw std::runtime_error(tr_fmt("cli.err.invalid_mode", "?"));
    } else {
        // ── direct 路径（原样保留）──
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
