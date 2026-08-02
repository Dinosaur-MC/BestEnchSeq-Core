#pragma once

#include "common/utils/cli/CLICommon.h"  // cli::ParseResult (for bind())
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/interface/BesqContext.h"
#include <optional>
#include <string>

// ============================================================================
// CLIApp — Full CLI Application
// ============================================================================
//
// Owns a BesqContext and runs the CLI workflow.  Created per-invocation.

class CLIApp {
public:
    CLIApp();
    int run(int argc, char* argv[]);
    static std::string detect_target(int argc, char* argv[]);

    // ── Parser types ──
    struct Config {
        std::string algorithm = "dp_merge";
        bool algorithm_explicit = false;  // true when --algorithm was given
        std::string mode      = "direct";
        std::string target;
        std::string source;
        std::string lang;
        std::string config_pairs;
        std::string algo_opt_pairs;      // --algo-opt k=v,k=v → SearchConfig::extra
        std::optional<std::string> input;
        std::optional<std::string> registry_dir;
        std::optional<std::string> registries;
        std::optional<std::string> registry_edit;
        std::optional<std::string> export_registry;
        std::optional<std::string> profile;
        std::optional<std::string> profile_dir;
        std::optional<std::string> publish;
        std::optional<std::string> publish_version;
        std::optional<std::string> publish_tag;
        std::optional<std::string> algo_dir;
        std::string platform = "auto";
        std::optional<std::string> output;
        std::string format   = "text";
        int solutions        = 1;
        int memory_mb        = 0;
        // nullopt = --max-time not provided (SearchConfig default 180s);
        //          0 = unlimited; >0 = seconds.
        std::optional<int> max_time;
        int max_threads      = 0;
        bool verbose         = false;
        bool help            = false;
        bool version         = false;
        bool brief_usage     = false;
        bool list_algorithms = false;
    };

    // ── Parser methods ──
    static Config parse(int argc, char* argv[]);
    static std::string help_text(std::string_view program_name = "besq");
    static void apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg);
    /// Parse --algo-opt k=v,k=v into SearchConfig::extra (strategy-specific knobs).
    static void apply_algo_opts(const std::string& algo_opts, algorithm::SearchConfig& cfg);

    /// Build a SolveRequest from a parsed Config (used by run() and tests).
    static SolveRequest build_solve_request(const Config& config, BesqContext& ctx);

    /// Select language from BESQ_LANG env var / system locale / --lang CLI flag.
    /// Call early (before parse) so errors use the correct language.
    static void apply_lang(int argc, char* argv[]);

private:
    struct UserI18nTranslator;

    static void apply_registry_edits(const std::string& spec, BesqContext& ctx);

    BesqContext _ctx;

    template<typename... Entries>
    static Config bind(const cli::ParseResult<Entries...>& result) {
        const auto& v = result.value;
        Config cfg;
        cfg.help            = std::get<0>(v);
        cfg.verbose         = std::get<1>(v);
        cfg.version         = std::get<2>(v);
        cfg.list_algorithms = std::get<3>(v);
        cfg.algorithm       = std::get<4>(v).value_or(Config{}.algorithm);
        cfg.algorithm_explicit = std::get<4>(v).has_value();
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
        cfg.max_time        = std::get<21>(v);  // nullopt when not provided
        cfg.max_threads     = std::get<22>(v).value_or(Config{}.max_threads);
        cfg.profile         = std::get<23>(v);
        cfg.profile_dir     = std::get<24>(v);
        cfg.publish         = std::get<25>(v);
        cfg.publish_version = std::get<26>(v);
        cfg.publish_tag     = std::get<27>(v);
        cfg.algo_opt_pairs  = std::get<28>(v).value_or(Config{}.algo_opt_pairs);
        return cfg;
    }
};
