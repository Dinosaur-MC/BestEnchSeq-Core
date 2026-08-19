#pragma once

#include "common/utils/cli/CLICommon.h"  // cli::ParseResult (for bind_*_result)
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/interface/BesqContext.h"
#include <optional>
#include <span>
#include <string>

// ============================================================================
// CLIApp — Full CLI Application
// ============================================================================
//
// Owns a BesqContext and runs the CLI workflow.  Created per-invocation.

class CLIApp {
public:
    CLIApp();
    ~CLIApp();
    int run(int argc, char* argv[]);

    // ── Parser types ──
    struct Config {
        enum class Cmd { solve, profile, algo, serve };
        enum class ProfileAction { none, list, set_dir, import, export_, info, publish };
        enum class AlgoAction { none, list, set_dir };

        Cmd cmd = Cmd::solve;
        std::string algorithm = "dp_merge";
        bool algorithm_explicit = false;
        bool profile_explicit   = false;
        std::string mode      = "direct";   // 内部语义保留：--source 推断（build_solve_request）
        std::string target;
        std::string source;
        std::string lang;
        std::string config_pairs;
        std::string algo_opt_pairs;
        std::optional<std::string> input;
        std::optional<std::string> output;
        std::optional<std::string> import_files;  // 保留（永不绑定）：test_cli_parser 仍引用；--import 迁至 profile 子命令（Task 5）
        std::optional<std::string> profile;
        std::optional<std::string> resume;
        std::string platform = "auto";
        std::string format   = "text";
        int solutions        = 1;
        int memory_mb        = 0;
        std::optional<int> max_time;
        int max_threads      = 0;
        bool verbose         = false;
        bool help            = false;
        bool version         = false;
        bool brief_usage     = false;
        bool list_langs      = false;
        // serve
        std::string serve_host;
        unsigned int serve_port = 0;   // 0 = 沿用 AppConfig http_port
        size_t serve_workers = 0;
        std::string serve_res_dir;
        // profile 动作
        ProfileAction profile_action = ProfileAction::none;
        std::string profile_target;      // set_dir <dir> / import <file> / info|publish <profile>
        std::string export_profile;      // export --profile <key>
        std::string export_file;         // export --file <path>
        std::string export_format = "json";
        std::string publish_version;     // publish --version（顶层不再有对应选项）
        std::string publish_tag;         // publish --tag（顶层不再有对应选项）
        // algo 动作
        AlgoAction algo_action = AlgoAction::none;
        std::string algo_target;         // set_dir <dir>
    };

    // ── Parser methods ──
    static Config parse(int argc, char* argv[]);
    static std::string help_text(std::string_view program_name = "besq");
    static std::string help_text(std::string_view program_name, std::span<const std::string_view> command_path);
    static void apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg);
    /// Parse --algo-opt k=v,k=v into SearchConfig::extra (strategy-specific knobs).
    static void apply_algo_opts(const std::string& algo_opts, algorithm::SearchConfig& cfg);

    /// Build a SolveRequest from a parsed Config (used by run() and tests).
    ///
    /// NOTE: In inventory mode this may activate the profile named in the JSON
    /// task (when no explicit `--profile` is given) — a side effect on the
    /// context's active profile. Callers must not assume the active profile is
    /// unchanged after the call.
    static SolveRequest build_solve_request(const Config& config, BesqContext& ctx);

    /// Select language from BESQ_LANG env var / system locale / --lang CLI flag.
    /// Call early (before parse) so errors use the correct language.
    static void apply_lang(int argc, char* argv[]);

private:
    struct UserI18nTranslator;

    /// Flush CLI stdout + drain the async Logger queue while the process is
    /// alive.  Exit-time static teardown ordering is unreliable in the
    /// EXE + SHARED besq-common-log layout, so the CLI must not depend on the
    /// implicit iostream flush at process exit.  Called after each output
    /// feature (list/export/solve/brief-usage) and again from ~CLIApp() as the
    /// RAII safety net (covers exceptions and any path that printed no banner).
    static void flush_output() noexcept;

    static void apply_edits(const std::string& spec, BesqContext& ctx);

    int run_profile(const Config& config);
    int run_algo(const Config& config);

    BesqContext _ctx;

    template<typename... Entries>
    static Config bind_solve_result(const cli::ParseResult<Entries...>& result) {
        const auto& v = result.value;
        Config cfg;
        cfg.help            = std::get<0>(v);
        cfg.verbose         = std::get<1>(v);
        cfg.version         = std::get<2>(v);
        cfg.list_langs      = std::get<3>(v);
        cfg.lang            = std::get<4>(v).value_or(Config{}.lang);
        cfg.algorithm       = std::get<5>(v).value_or(Config{}.algorithm);
        cfg.algorithm_explicit = std::get<5>(v).has_value();
        cfg.target          = std::get<6>(v).value_or(Config{}.target);
        cfg.source          = std::get<7>(v).value_or(Config{}.source);
        cfg.platform        = std::get<8>(v).value_or(Config{}.platform);
        cfg.format          = std::get<9>(v).value_or(Config{}.format);
        cfg.solutions       = std::get<10>(v).value_or(Config{}.solutions);
        cfg.memory_mb       = 0;   // 由 post_bind_solve 从 --memory 字符串解析
        cfg.max_time        = std::get<12>(v);
        cfg.max_threads     = std::get<13>(v).value_or(Config{}.max_threads);
        cfg.algo_opt_pairs  = std::get<14>(v).value_or(Config{}.algo_opt_pairs);
        cfg.input           = std::get<15>(v);
        cfg.output          = std::get<16>(v);
        cfg.resume          = std::get<17>(v);
        cfg.config_pairs    = std::get<18>(v).value_or(Config{}.config_pairs);
        cfg.profile         = std::get<19>(v);
        return cfg;
    }

    template<typename... Entries>
    static Config bind_profile_result(const cli::ParseResult<Entries...>& result) {
        const auto& v = result.value;
        Config cfg;
        cfg.cmd = Config::Cmd::profile;
        cfg.help    = std::get<0>(v);
        cfg.verbose = std::get<1>(v);
        cfg.version = std::get<2>(v);
        // 注意：Positional 缺失时槽位为 nullopt——一律 value_or，**禁止**裸解引用
        if (std::get<3>(v).has_value())                       cfg.profile_action = Config::ProfileAction::list;
        else if (std::get<4>(v).has_value()) {                cfg.profile_action = Config::ProfileAction::set_dir;
            cfg.profile_target = std::get<0>(std::get<4>(v)->value).value_or(Config{}.profile_target); }
        else if (std::get<5>(v).has_value()) {                cfg.profile_action = Config::ProfileAction::import;
            cfg.profile_target = std::get<0>(std::get<5>(v)->value).value_or(Config{}.profile_target); }
        else if (std::get<6>(v).has_value()) {                cfg.profile_action = Config::ProfileAction::export_;
            const auto& e = *std::get<6>(v);
            cfg.export_profile = std::get<0>(e.value).value_or(Config{}.export_profile);
            cfg.export_file    = std::get<1>(e.value).value_or(Config{}.export_file);
            cfg.export_format  = std::get<2>(e.value).value_or(Config{}.export_format); }
        else if (std::get<7>(v).has_value()) {                cfg.profile_action = Config::ProfileAction::info;
            cfg.profile_target = std::get<0>(std::get<7>(v)->value).value_or(Config{}.profile_target); }
        else if (std::get<8>(v).has_value()) {                cfg.profile_action = Config::ProfileAction::publish;
            const auto& p = *std::get<8>(v);
            cfg.profile_target = std::get<0>(p.value).value_or(Config{}.profile_target);
            cfg.publish_version = std::get<1>(p.value).value_or(Config{}.publish_version);
            cfg.publish_tag     = std::get<2>(p.value).value_or(Config{}.publish_tag); }
        return cfg;
    }

    template<typename... Entries>
    static Config bind_algo_result(const cli::ParseResult<Entries...>& result) {
        const auto& v = result.value;
        Config cfg;
        cfg.cmd = Config::Cmd::algo;
        cfg.help    = std::get<0>(v);
        cfg.verbose = std::get<1>(v);
        cfg.version = std::get<2>(v);
        if (std::get<3>(v).has_value())                       cfg.algo_action = Config::AlgoAction::list;
        else if (std::get<4>(v).has_value()) {                cfg.algo_action = Config::AlgoAction::set_dir;
            cfg.algo_target = std::get<0>(std::get<4>(v)->value).value_or(Config{}.algo_target); }
        return cfg;
    }

    template<typename... Entries>
    static Config bind_serve_result(const cli::ParseResult<Entries...>& result) {
        const auto& v = result.value;
        Config cfg;
        cfg.cmd = Config::Cmd::serve;
        cfg.help       = std::get<0>(v);
        cfg.verbose    = std::get<1>(v);
        cfg.version    = std::get<2>(v);
        cfg.serve_host = std::get<3>(v).value_or(Config{}.serve_host);
        cfg.serve_port = std::get<4>(v).value_or(Config{}.serve_port);
        cfg.serve_workers = std::get<5>(v).value_or(Config{}.serve_workers);
        cfg.serve_res_dir = std::get<6>(v).value_or(Config{}.serve_res_dir);
        return cfg;
    }
};
