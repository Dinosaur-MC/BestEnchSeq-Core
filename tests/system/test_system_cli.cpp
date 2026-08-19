// =============================================================================
// System tests: spawn the real `besq` CLI binary and assert stdout/stderr/exit.
//
// SKIP layering (mirrors tests/domain/algorithm/test_sandbox.cpp):
//   - besq binary missing        → whole file SKIP()（框架计数跳过）
//   - plugin files missing       → 局部 cout-SKIP 提前 return（SKIP() 会中止整个
//                                   合一 case，故保留非中止式跳过，见 case 体）
//   - besq-worker missing        → 同插件分支
// =============================================================================

#define BESQ_TEST_MAIN

#include "framework/test_framework.h"
#include "spawn_util.h"

#include "AppConfig.h"
#include "common/utils/EnvUtil.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

// ── binary / plugin / worker discovery ────────────────────────────────────

std::string find_besq() {
    const std::string env_bin = get_env<std::string>("BESQ_BIN_PATH", "");
    if (!env_bin.empty() && std::filesystem::exists(env_bin))
        return env_bin;
#ifdef BESQ_BIN_PATH
    if (std::filesystem::exists(BESQ_BIN_PATH))
        return BESQ_BIN_PATH;
#endif
#ifdef BESQ_PROJECT_ROOT
    const char* cand[] = {
#if defined(_WIN32)
        "build/bin/besq.exe",
#else
        "build-wsl/bin/besq",
        "build/bin/besq",
#endif
    };
    const std::string root = BESQ_PROJECT_ROOT;
    for (auto* c : cand)
        if (std::filesystem::exists(root + "/" + c))
            return root + "/" + c;
#endif
    return {};
}

std::string find_plugin_dir() {
#ifdef BESQ_PROJECT_ROOT
    const char* cand[] = {
#if defined(_WIN32)
        "build/plugins",
#else
        "build-wsl/plugins",
        "build/plugins",
#endif
    };
    const std::string root = BESQ_PROJECT_ROOT;
    for (auto* c : cand)
        if (std::filesystem::is_directory(root + "/" + c))
            return root + "/" + c;
#endif
    return {};
}

bool exists_plugin(const std::string& dir, const char* name) {
#if defined(_WIN32)
    return std::filesystem::exists(dir + "/algo_" + name + ".dll");
#else
    return std::filesystem::exists(dir + "/libalgo_" + name + ".so");
#endif
}

std::string find_worker() {
    const std::string env_w = get_env<std::string>("BESQ_WORKER_PATH", "");
    if (!env_w.empty())
        return env_w;
#ifdef BESQ_PROJECT_ROOT
    const char* cand[] = {
#if defined(_WIN32)
        "build/bin/besq-worker.exe",
#else
        "build-wsl/bin/besq-worker",
        "build/bin/besq-worker",
#endif
    };
    const std::string root = BESQ_PROJECT_ROOT;
    for (auto* c : cand)
        if (std::filesystem::exists(root + "/" + c))
            return root + "/" + c;
#endif
    return {};
}

// ── spawn helpers ─────────────────────────────────────────────────────────

using besq_test::run_cli;
using besq_test::RunResult;

RunResult run_besq(const std::string& bin,
                   std::vector<std::string> args,
                   const std::string& stdin_input = {},
                   std::vector<std::pair<std::string, std::string>> extra_env = {}) {
    std::vector<std::string> full{bin};
    full.insert(full.end(), args.begin(), args.end());
    return run_cli(full, stdin_input, extra_env);
}

void expect_contains(const std::string& haystack, const std::string& needle, const std::string& msg) {
    expect(haystack.find(needle) != std::string::npos, msg);
}

// Common fast-solve flag bundle for search-path cases (bounds a hung child;
// the popen harness itself cannot kill, so besq's own --max-time is the
// application-level backstop, with CTest TIMEOUT 300 as the coarse one).
const char* kMaxTime = "--max-time";
const char* kMaxTimeVal = "120";

// ── core cases (SKIP condition: besq binary missing) ──────────────────────

void test_no_args(const std::string& bin) {
    // Keep argv empty (a true "no args" invocation); pin the locale via env so
    // the usage text is deterministic regardless of the host system locale.
    auto r = run_besq(bin, {}, /*stdin_input=*/{}, {{"BESQ_LANG", "en_US"}});
    expect_eq(r.exit_code, 0, "no args: exit 0");
    expect_contains(r.out, "Usage:", "no args: stdout has Usage:");
    expect_contains(r.out, "--target", "no args: stdout mentions --target");
    TEST_PASS("system: no-args usage");
}

void test_help(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--help"});
    expect_eq(r.exit_code, 0, "--help: exit 0");
    expect_contains(r.out, "Usage:", "--help: stdout has Usage:");
    expect_contains(r.out, "[options]", "--help: stdout has [options]");
    expect_contains(r.out, "--target", "--help: stdout mentions --target");
    expect_contains(r.out, "Examples:", "--help: stdout has Examples:");
    TEST_PASS("system: --help");
}

void test_version(const std::string& bin) {
    auto r = run_besq(bin, {"--version"});
    expect_eq(r.exit_code, 0, "--version: exit 0");
    expect_contains(r.out, "BestEnchSeq-Core v", "--version: stdout has version line");
    TEST_PASS("system: --version");
}

void test_list_algorithms(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "algo", "list"});
    expect_eq(r.exit_code, 0, "algo list: exit 0");
    expect_contains(r.out, "Available algorithm strategies (", "algo list: header");
    expect_contains(r.out, "bb_dp", "algo list: has bb_dp");
    expect_contains(r.out, "dp_merge", "algo list: has dp_merge");
    expect_contains(r.out, "hamming", "algo list: has hamming");
    TEST_PASS("system: algo list");
}

void test_list_profiles_langs(const std::string& bin) {
    // profile list: auto-load runs, builtin:vanilla is always present.
    auto rp = run_besq(bin, {"--lang", "en_US", "profile", "list"});
    expect_eq(rp.exit_code, 0, "profile list: exit 0");
    expect_contains(rp.out, "Available profiles (", "profile list: header");
    expect_contains(rp.out, "builtin:vanilla", "profile list: has builtin:vanilla");
    // --list-langs: built-in languages are always available.
    auto rl = run_besq(bin, {"--lang", "en_US", "--list-langs"});
    expect_eq(rl.exit_code, 0, "--list-langs: exit 0");
    expect_contains(rl.out, "Available languages (", "--list-langs: header");
    expect_contains(rl.out, "en_US", "--list-langs: has en_US");
    expect_contains(rl.out, "zh_CN", "--list-langs: has zh_CN");
    TEST_PASS("system: profile list / --list-langs");
}

void test_profile_group_combo(const std::string& bin) {
    // Composite profile end-to-end: two temp profiles (combo_a: mod:ember,
    // combo_b: mod:flame, both depending on vanilla) combined via `--profile a,b`.
    auto tmp = std::filesystem::temp_directory_path() / "besq_sys_combo";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream f(tmp / "combo_a.json");
        f << R"({"name":"combo_a","dependencies":["builtin:vanilla"],
                  "enchantments":[{"id":"mod:ember","name":"Ember","platform":"java",
                                   "max_level":3,"multiplier":2,
                                   "supported_items":["#minecraft:swords"]}]})";
    }
    {
        std::ofstream f(tmp / "combo_b.json");
        f << R"({"name":"combo_b","dependencies":["builtin:vanilla"],
                  "enchantments":[{"id":"mod:flame","name":"Flame","platform":"java",
                                   "max_level":2,"multiplier":2,
                                   "supported_items":["#minecraft:swords"]}]})";
    }

    // profile list（BESQ_PROFILES_DIR env 指定目录）：两个成员都在列表里。
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "list"}, /*stdin_input=*/{},
                      {{"BESQ_PROFILES_DIR", tmp.string()}});
    expect_eq(r.exit_code, 0, "profile list (env dir): exit 0");
    expect_contains(r.out, "combo_a", "profile list shows combo_a");
    expect_contains(r.out, "combo_b", "profile list shows combo_b");

    // Composite solve（BESQ_PROFILES_DIR env）：目标使用两个成员的魔咒 → 可达。
    // --verbose 让 text 输出带上原始 NSID（"Flame (mod:flame)"）；不带则只打
    // 显示名 "Flame"。
    auto r2 = run_besq(bin, {"--lang", "en_US", "--verbose", kMaxTime, kMaxTimeVal,
                             "--profile", "combo_a,combo_b", "--target", "diamond_sword[mod:ember=1,mod:flame=1]",
                             "--source", "mod:ember=1"},
                       /*stdin_input=*/{}, {{"BESQ_PROFILES_DIR", tmp.string()}});
    expect_eq(r2.exit_code, 0, "composite solve: exit 0");
    expect_contains(r2.out, "mod:flame", "composite solve uses combo_b enchant");

    // Composite with an unknown member → rejected（solve 域激活阶段）。
    auto r4 = run_besq(bin, {"--lang", "en_US", "--profile", "combo_a,missing",
                             "--target", "diamond_sword[sharpness=1]"},
                       /*stdin_input=*/{}, {{"BESQ_PROFILES_DIR", tmp.string()}});
    expect(r4.exit_code != 0, "composite with unknown member rejected");

    std::filesystem::remove_all(tmp);
}

void test_solve_text(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", kMaxTime, kMaxTimeVal, "--target", "diamond_sword[sharpness=5,knockback=2]"});
    expect_eq(r.exit_code, 0, "solve text: exit 0");
    expect_contains(r.out, "Forge Plan", "solve text: Forge Plan header");
    expect_contains(r.out, "Mode: ", "solve text: Mode line");
    expect_contains(r.out, "Total cost: ", "solve text: Total cost line");
    expect_contains(r.out, "Final Item: ", "solve text: Final Item line");
    expect_contains(r.out, "Target Item: ", "solve text: Target Item line");
    TEST_PASS("system: solve verbose text");
}

void test_solve_compact(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", kMaxTime, kMaxTimeVal, "--format", "compact", "--target",
                            "diamond_sword[sharpness=5]", "--solutions", "1"});
    expect_eq(r.exit_code, 0, "solve compact: exit 0");
    expect_contains(r.out, "#MODE=direct", "solve compact: MODE line");
    expect_contains(r.out, "#PLATFORM=", "solve compact: PLATFORM line");
    expect_contains(r.out, "#SOLUTIONS=", "solve compact: SOLUTIONS line");
    TEST_PASS("system: solve compact");
}

void test_solve_json(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", kMaxTime, kMaxTimeVal, "--format", "json", "--target",
                            "diamond_sword[sharpness=5]", "--solutions", "1"});
    expect_eq(r.exit_code, 0, "solve json: exit 0");
    expect_contains(r.out, "\"schema_version\"", "solve json: schema_version");
    expect_contains(r.out, "\"mode\"", "solve json: mode key");
    expect_contains(r.out, "\"direct\"", "solve json: mode value direct");
    expect_contains(r.out, "\"success\"", "solve json: success key");
    expect_contains(r.out, "\"solutions\"", "solve json: solutions key");
    TEST_PASS("system: solve json");
}

void test_solve_already_met(const std::string& bin) {
    auto r = run_besq(
        bin, {"--lang", "en_US", kMaxTime, kMaxTimeVal, "--source", "sharpness=5", "--target", "diamond_sword[sharpness=5]"});
    expect_eq(r.exit_code, 0, "already-met: exit 0");
    expect_contains(r.out, "Goal already met", "already-met: 0-step notice");
    TEST_PASS("system: already-met 0-step");
}

void test_err_source_without_target(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--source", "sharpness=5"});
    expect_eq(r.exit_code, 1, "err source-without-target: exit 1");
    expect_contains(r.err, "Error: ", "err source-without-target: error prefix");
    expect_contains(r.err, "--source requires --target", "err source-without-target: message");
    TEST_PASS("system: error --source without --target");
}

void test_err_invalid_format(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--format", "foo", "--target", "diamond_sword[sharpness=1]"});
    expect_eq(r.exit_code, 1, "err invalid format: exit 1");
    expect_contains(r.err, "Invalid format", "err invalid format: message");
    TEST_PASS("system: error --format foo");
}

void test_err_unknown_algo(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--algorithm", "nope", "--target", "diamond_sword[sharpness=1]"});
    expect_eq(r.exit_code, 1, "err unknown algo: exit 1");
    expect_contains(r.err, "Unknown algorithm: 'nope'", "err unknown algo: message");
    TEST_PASS("system: error unknown algorithm");
}

void test_err_unknown_ench(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--target", "diamond_sword[foo=1]"});
    expect_eq(r.exit_code, 1, "err unknown ench: exit 1");
    expect_contains(r.err, "Unknown enchantment: 'foo'", "err unknown ench: message");
    TEST_PASS("system: error unknown enchantment");
}

void test_err_unknown_equipment(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--target", "not_a_real_item[sharpness=1]"});
    expect_eq(r.exit_code, 1, "err unknown equipment: exit 1");
    expect_contains(r.err, "Unknown equipment: 'not_a_real_item'", "err unknown equipment: message");
    TEST_PASS("system: error unknown equipment");
}

void test_err_missing_target(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "--solutions", "2"});
    expect_eq(r.exit_code, 1, "err missing target: exit 1");
    expect_contains(r.err, "Missing --target", "err missing target: message");
    TEST_PASS("system: error missing target");
}

void test_err_unreachable(const std::string& bin) {
    // sharpness and smite are mutually exclusive (vanilla.json exclusive_set);
    // the parse succeeds but the solve yields no plan → "Target unreachable".
    auto r = run_besq(bin, {"--lang", "en_US", kMaxTime, kMaxTimeVal, "--target", "diamond_sword[sharpness=5,smite=5]"});
    expect_eq(r.exit_code, 1, "err unreachable: exit 1");
    expect_contains(r.err, "Target unreachable", "err unreachable: message");
    TEST_PASS("system: error target unreachable");
}

void test_input_stdin(const std::string& bin) {
    const std::string task = "{\"target\":{\"item\":\"diamond_sword\","
                             "\"enchants\":[{\"id\":\"sharpness\",\"level\":5}]},"
                             "\"items\":["
                             "{\"type\":\"book\",\"enchants\":[{\"id\":\"sharpness\",\"level\":5}],"
                             "\"prior_penalty\":0},"
                             "{\"type\":\"equipment\",\"id\":\"diamond_sword\"}"
                             "],"
                             "\"algorithm\":\"hamming\"}";
    auto r = run_besq(bin, {"--lang", "en_US", "--input", "-"}, task);
    expect_eq(r.exit_code, 0, "--input -: exit 0");
    expect_contains(r.out, "Forge Plan", "--input -: solves to a plan");
    TEST_PASS("system: --input - reads stdin");
}

void test_export_stdout(const std::string& bin) {
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "export", "--file", "-"});
    expect_eq(r.exit_code, 0, "profile export --file -: exit 0");
    expect(!r.out.empty() && r.out.front() == '{', "profile export: stdout is JSON starting with {");
    expect_contains(r.out, "\"enchantments\"", "profile export: has enchantments");
    TEST_PASS("system: profile export writes stdout");
}

// ── slice-1 子命令化 system 用例 ─────────────────────────────────────────

void test_serve_subcommand_smoke(const std::string& bin) {
    auto r = run_besq(bin, {"serve", "--help"});
    expect_eq(r.exit_code, 0, "serve --help: exit 0");
    expect_contains(r.out, "--port", "serve --help lists --port");
    TEST_PASS("system: serve --help");
}

void test_api_serve_removed(const std::string& bin) {
    auto r = run_besq(bin, {"--api", "serve"});
    expect(r.exit_code != 0, "--api serve rejected");
    TEST_PASS("system: --api serve removed");
}

void test_set_dir_persistence_roundtrip(const std::string& bin) {
    // 备份现有 config.json（若有），RAII 结束恢复——断言失败/异常路径也恢复。
    const std::string cfg_path = AppConfig::config_file_path();
    const std::string backup = cfg_path + ".bak_slice1";
    const bool had = std::filesystem::exists(cfg_path);
    if (had) std::filesystem::copy_file(cfg_path, backup, std::filesystem::copy_options::overwrite_existing);
    struct CfgGuard {
        bool had;
        std::string cfg_path, backup;
        ~CfgGuard() {
            std::error_code ec;
            if (had) std::filesystem::copy_file(backup, cfg_path, std::filesystem::copy_options::overwrite_existing, ec);
            else std::filesystem::remove(cfg_path, ec);
            std::filesystem::remove(backup, ec);   // 清掉备份文件本身
        }
    } guard{had, cfg_path, backup};

    // generic_string：Windows 反斜杠会被 JSON 序列化转义（\\），原始子串断言
    // 对不上——统一用正斜杠形式（POSIX 本即正斜杠）。
    const std::string tmp = (std::filesystem::temp_directory_path() / "besq_pdir_test").generic_string();
    auto r = run_besq(bin, {"profile", "set_dir", tmp});
    expect_eq(r.exit_code, 0, "profile set_dir: exit 0");
    expect(std::filesystem::exists(cfg_path), "config.json written");
    std::string content;
    { std::ifstream in(cfg_path); content.assign(std::istreambuf_iterator<char>(in), {}); }
    expect_contains(content, "\"profiles_dir\"", "config.json has profiles_dir");
    expect_contains(content, tmp, "config.json has the dir value");
    TEST_PASS("system: set_dir persistence roundtrip");
}

// ── plugin cases (SKIP: plugin files missing) ─────────────────────────────

void test_plugin_list(const std::string& bin) {
    const std::string dir = find_plugin_dir();
    if (dir.empty() || !exists_plugin(dir, "astar")) {
        std::cout << "SKIP: plugins not built (cmake --build build/plugins)" << std::endl;
        return;
    }
    auto r = run_besq(bin, {"--lang", "en_US", "algo", "list"}, /*stdin_input=*/{},
                      {{"BESQ_ALGO_DIR", dir}});
    expect_eq(r.exit_code, 0, "plugin list: exit 0");
    expect_contains(r.out, "astar", "plugin list: astar registered from plugin");
    TEST_PASS("system: plugin algo list (BESQ_ALGO_DIR)");
}

void test_plugin_solve(const std::string& bin) {
    const std::string dir = find_plugin_dir();
    if (dir.empty() || !exists_plugin(dir, "astar")) {
        std::cout << "SKIP: plugins not built (cmake --build build/plugins)" << std::endl;
        return;
    }
    auto r = run_besq(bin, {"--lang", "en_US", "--algorithm", "astar", kMaxTime, kMaxTimeVal, "--target",
                            "diamond_sword[sharpness=5,knockback=2]"},
                      /*stdin_input=*/{}, {{"BESQ_ALGO_DIR", dir}});
    expect_eq(r.exit_code, 0, "plugin solve: exit 0");
    expect_contains(r.out, "Forge Plan", "plugin solve: astar produced a plan");
    TEST_PASS("system: plugin astar solve");
}

void test_plugin_malicious_refused(const std::string& bin) {
    const std::string dir = find_plugin_dir();
    if (dir.empty() || !exists_plugin(dir, "malicious")) {
        std::cout << "SKIP: malicious plugin not built (cmake --build "
                     "build/plugins)"
                  << std::endl;
        return;
    }
    // --verbose: console logging is OFF by default, and the audit refusal is
    // an ERROR log — visible on stderr only when console logging is enabled.
    // (Verbose audit DEBUG lines may mention the plugin path on stdout, so
    // assert on the strategy LIST line ("  name" prefix) instead of the raw
    // stream.)
    // --verbose must follow the subcommand token (`algo --verbose list`):
    // pre-command flags bind to the top-level (solve) table and are discarded
    // when the algo sub-result is dispatched.
    auto r = run_besq(bin, {"--lang", "en_US", "algo", "--verbose", "list"}, /*stdin_input=*/{},
                      {{"BESQ_ALGO_DIR", dir}});
    expect_eq(r.exit_code, 0, "plugin audit-refused: list still exits 0");
    expect_contains(r.err, "[Audit] REFUSED", "plugin audit-refused: stderr marks REFUSED");
    expect(r.out.find("  malicious") == std::string::npos,
           "plugin audit-refused: malicious NOT in the strategy list");
    TEST_PASS("system: malicious plugin refused by audit");
}

// ── sandbox cases (SKIP: besq-worker missing) ─────────────────────────────

void test_sandbox_astar(const std::string& bin) {
    if (find_worker().empty()) {
        std::cout << "SKIP: besq-worker not built (BESQ_BUILD_SANDBOX=ON)" << std::endl;
        return;
    }
    const std::string dir = find_plugin_dir();
    if (dir.empty() || !exists_plugin(dir, "astar")) {
        std::cout << "SKIP: plugins not built (cmake --build build/plugins)" << std::endl;
        return;
    }
    auto r = run_besq(bin,
                      {"--lang", "en_US", "--algorithm", "astar", kMaxTime, kMaxTimeVal, "--target",
                       "diamond_sword[sharpness=5]"},
                      /*stdin_input=*/{}, {{"BESQ_ALGO_DIR", dir}, {"BESQ_SANDBOX", "1"}});
    expect_eq(r.exit_code, 0, "sandbox astar: exit 0");
    expect_contains(r.out, "Forge Plan", "sandbox astar: solved in worker");
    TEST_PASS("system: BESQ_SANDBOX=1 astar solve");
}

void test_sandbox_malicious_listed(const std::string& bin) {
    if (find_worker().empty()) {
        std::cout << "SKIP: besq-worker not built (BESQ_BUILD_SANDBOX=ON)" << std::endl;
        return;
    }
    const std::string dir = find_plugin_dir();
    if (dir.empty() || !exists_plugin(dir, "malicious")) {
        std::cout << "SKIP: plugins not built (cmake --build build/plugins)" << std::endl;
        return;
    }
    auto r = run_besq(bin, {"--lang", "en_US", "algo", "list"},
                      /*stdin_input=*/{}, {{"BESQ_ALGO_DIR", dir}, {"BESQ_SANDBOX", "1"}});
    expect_eq(r.exit_code, 0, "sandbox malicious: list exits 0");
    // In sandbox mode the plugin runs contained in the worker (parent never
    // dlopens it), so the audit does NOT refuse it → it gets listed.
    expect_contains(r.out, "malicious", "sandbox malicious: plugin loaded in worker");
    expect(r.err.find("[Audit] REFUSED") == std::string::npos, "sandbox malicious: no audit refusal under sandbox");
    TEST_PASS("system: BESQ_SANDBOX=1 malicious loads contained");
}

// Only meaningful when the worker is ABSENT: BESQ_SANDBOX=1 with a plugin
// directory then fails to probe → the algorithm is unknown (exit 1).
void test_sandbox_worker_missing_fallback(const std::string& bin) {
    if (!find_worker().empty()) {
        std::cout << "SKIP: besq-worker present — fallback path not exercised" << std::endl;
        return;
    }
    const std::string dir = find_plugin_dir();
    if (dir.empty() || !exists_plugin(dir, "malicious")) {
        std::cout << "SKIP: plugins not built (cmake --build build/plugins)" << std::endl;
        return;
    }
    auto r = run_besq(
        bin, {"--lang", "en_US", "--algorithm", "malicious", "--target", "diamond_sword[sharpness=1]"},
        /*stdin_input=*/{}, {{"BESQ_ALGO_DIR", dir}, {"BESQ_SANDBOX", "1"}});
    expect_eq(r.exit_code, 1, "sandbox no-worker: exit 1");
    expect_contains(r.err, "Unknown algorithm", "sandbox no-worker: algorithm never registered");
    TEST_PASS("system: BESQ_SANDBOX=1 without worker falls back to unknown-algo");
}

} // anonymous namespace

TEST_CASE("test_system_cli") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }

    // core
    test_no_args(bin);
    test_help(bin);
    test_version(bin);
    test_list_algorithms(bin);
    test_list_profiles_langs(bin);
    test_profile_group_combo(bin);
    test_solve_text(bin);
    test_solve_compact(bin);
    test_solve_json(bin);
    test_solve_already_met(bin);
    test_err_source_without_target(bin);
    test_err_invalid_format(bin);
    test_err_unknown_algo(bin);
    test_err_unknown_ench(bin);
    test_err_unknown_equipment(bin);
    test_err_missing_target(bin);
    test_err_unreachable(bin);
    test_input_stdin(bin);
    test_export_stdout(bin);
    // slice-1 子命令化
    test_serve_subcommand_smoke(bin);
    test_api_serve_removed(bin);
    test_set_dir_persistence_roundtrip(bin);
    // plugins (self-skip)
    test_plugin_list(bin);
    test_plugin_solve(bin);
    test_plugin_malicious_refused(bin);
    // sandbox (self-skip)
    test_sandbox_astar(bin);
    test_sandbox_malicious_listed(bin);
    test_sandbox_worker_missing_fallback(bin);
}
