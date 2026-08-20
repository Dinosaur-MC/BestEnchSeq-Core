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

#include <chrono>
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

/// 跨平台唯一时间戳后缀（temp 目录唯一性）：Windows 用 GetTickCount64
/// （test_framework.h / spawn_util.h 已含 windows.h，同 test_cli_acceptance.cpp
/// 先例），其他平台回退 steady_clock 毫秒（保持 WSL 构建可编译）。
static std::string unique_ts_suffix() {
#ifdef _WIN32
    return std::to_string(static_cast<long long>(::GetTickCount64()));
#else
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count());
#endif
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

// ── slice-2a 新增 e2e（独立 TEST_CASE，--filter 可按 case 粒度匹配）─────
//
// 注：text 形态不携带 --limit —— EnchantmentRegistry 底层是
// std::unordered_map（"No positional index — entries are identified by NSID
// key"，IRegistry.h），p.ench() 迭代序为哈希桶序，sharpness 不在前 5 行内；
// --limit 分页已在 test_cli_acceptance 覆盖（--limit 3/5 + --page）。此处
// 打印全表保证 "max_level" 表头 + sharpness 行确定性出现。

TEST_CASE("system: profile inspect e2e") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    auto r = run_besq(bin, {"profile", "inspect", "builtin:vanilla", "ench"});
    expect_eq(r.exit_code, 0, "inspect: exit 0");
    expect_contains(r.out, "max_level", "inspect: table header");
    expect_contains(r.out, "sharpness", "inspect: has sharpness");
    auto rj = run_besq(bin, {"profile", "inspect", "builtin:vanilla", "ench", "--filter", "sharpness", "--format", "json"});
    expect_eq(rj.exit_code, 0, "inspect json: exit 0");
    expect_contains(rj.out, "\"kind\"", "inspect json: kind field");
    expect_contains(rj.out, "sharpness", "inspect json: filtered row");
    TEST_PASS("system: profile inspect");
}

TEST_CASE("system: algo inspect e2e") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    auto r = run_besq(bin, {"algo", "inspect", "dp_merge"});
    expect_eq(r.exit_code, 0, "algo inspect: exit 0");
    expect_contains(r.out, "dp_merge", "algo inspect: name");
    TEST_PASS("system: algo inspect");
}

TEST_CASE("system: datapack export e2e") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // temp 目录导出 → 目录结构存在（ns = "builtin" 不破坏目录创建）
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_dp_sys_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    auto r = run_besq(bin, {"profile", "export", "--format", "datapack", "--file", tmp, "--profile", "builtin:vanilla"});
    expect_eq(r.exit_code, 0, "datapack export: exit 0");
    expect(std::filesystem::exists(tmp + "/pack.mcmeta"), "pack.mcmeta exists");
    expect(std::filesystem::exists(tmp + "/data"), "data dir exists");
    TEST_PASS("system: datapack export");
}

// ── slice-1 profile sql 系统 e2e（真实 CLI 端到端）──────────────────────
//
// 链式执行 + json 数组输出；INSERT → SAVE 写盘 → 新进程 SELECT 见新行
// （跨进程持久化门：SAVE 文件名消毒 `:`→`_`（Task 4），回读靠文件内 name
// 字段恢复 profile；bare enchantment INSERT 不受 FK 严格检查约束）。

TEST_CASE("system: profile sql chain") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    auto r = run_besq(bin, {"profile", "sql", "SELECT id FROM enchantment LIMIT 2"});
    expect_eq(r.exit_code, 0, "sql chain: exit 0");
    expect_contains(r.out, "id", "sql chain: header");
    auto rj = run_besq(bin, {"profile", "sql", "--format", "json", "SELECT id FROM enchantment LIMIT 1"});
    expect_eq(rj.exit_code, 0, "sql json: exit 0");
    expect_contains(rj.out, "[", "sql json: array");
    TEST_PASS("system: profile sql");
}

TEST_CASE("system: profile sql save roundtrip") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // temp profiles dir：INSERT → SAVE 写盘 → 新进程 SELECT 见新行（持久化回读门）
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string env_ns = "test";
    auto r1 = run_besq(bin,
        {"profile", "sql",
         "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES ('" + env_ns + ":sqlmark','SQLMark',1,1,'#minecraft:swords'); SAVE"},
        {}, {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r1.exit_code, 0, "sql insert+save: exit 0");
    auto r2 = run_besq(bin, {"profile", "sql", "SELECT id FROM enchantment WHERE id='" + env_ns + ":sqlmark'"},
                       {}, {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r2.exit_code, 0, "sql reload select: exit 0");
    expect_contains(r2.out, env_ns + ":sqlmark", "saved row visible after reload");
    TEST_PASS("system: profile sql save roundtrip");
}

// ── slice-2 跨 profile 系统 e2e（真实 CLI：USE/COPY/FORK/MERGE + SAVE roundtrip）──
//
// 跨进程持久化门：COPY/FORK 产物经 SAVE 落盘 → 新进程（重新加载 profiles 目录）
// 可见；MERGE 目标标脏 → STATUS 输出带 (dirty)。临时 profiles 目录隔离
// （BESQ_PROFILES_DIR → temp 目录）+ unique_ts_suffix 唯一 profile 名（防跨测试
// 干扰）。种子 profile 用最小 native JSON（加载时保留 vanilla tag 宇宙 → 其
// supported_items '#minecraft:swords' 引用在 INSERT/COPY 的 FK 校验下可解析，
// 与 acceptance 的 use_a.json 先例同源）。

TEST_CASE("system: profile sql cross USE chain") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_use_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p2 = "besq_use_p2_" + unique_ts_suffix();
    {
        std::ofstream f(std::filesystem::path(tmp) / (p2 + ".json"));
        f << "{\"name\":\"" << p2 << "\",\"enchantments\":[],\"equipments\":[],\"tags\":{}}";
    }
    // 链：USE p2（会话切换）→ INSERT（写入 p2）→ SELECT（从 p2 回读见行）
    const std::string stmt = "USE " + p2 + "; "
        "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
        "VALUES ('test:chainmark','ChainMark',1,1,'#minecraft:swords'); "
        "SELECT id FROM enchantment WHERE id='test:chainmark';";
    auto r = run_besq(bin, {"profile", "sql", stmt}, {},
                      {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r.exit_code, 0, "use chain: exit 0");
    expect_contains(r.out, "use: " + p2, "use chain: USE message");
    expect_contains(r.out, "test:chainmark", "use chain: SELECT sees the inserted row");
    // 脏跟踪切到 p2（未 SAVE → 退出警告点名 p2，而非默认 vanilla）
    expect_contains(r.err, "unsaved changes in: " + p2, "use chain: dirty tracks p2");
    TEST_PASS("system: profile sql cross USE chain");
}

TEST_CASE("system: profile sql cross COPY roundtrip") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_copy_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p1 = "besq_copy_p1_" + unique_ts_suffix();
    const std::string p2 = "besq_copy_p2_" + unique_ts_suffix();
    {
        // p1 空目标；p2 源含一行 test:copymark（strict FK 对 p1 校验时
        // '#minecraft:swords' 经 p1 保留的 vanilla tag 宇宙解析）
        std::ofstream f1(std::filesystem::path(tmp) / (p1 + ".json"));
        f1 << "{\"name\":\"" << p1 << "\",\"enchantments\":[],\"equipments\":[],\"tags\":{}}";
        std::ofstream f2(std::filesystem::path(tmp) / (p2 + ".json"));
        f2 << "{\"name\":\"" << p2 << "\",\"enchantments\":[{\"id\":\"test:copymark\",\"name\":\"CopyMark\","
              "\"platform\":\"java\",\"max_level\":1,\"multiplier\":1,"
              "\"supported_items\":[\"#minecraft:swords\"]}],\"equipments\":[],\"tags\":{}}";
    }
    // 进程 1：USE p1 → COPY * FROM p2 INTO enchantment → SAVE（p1.json 落盘）
    const std::string stmt = "USE " + p1 + "; COPY * FROM " + p2 + " INTO enchantment; SAVE";
    auto r1 = run_besq(bin, {"profile", "sql", stmt}, {},
                       {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r1.exit_code, 0, "copy+save: exit 0");
    expect_contains(r1.out, "row(s) affected", "copy: affected message");
    expect_contains(r1.out, "saved: " + p1, "copy: save message");
    // 进程 2（新进程）：--profile p1 SELECT 见复制行 —— 跨进程 roundtrip 门
    auto r2 = run_besq(bin,
                       {"profile", "sql", "SELECT id FROM enchantment WHERE id='test:copymark'", "--profile", p1},
                       {}, {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r2.exit_code, 0, "copy roundtrip select: exit 0");
    expect_contains(r2.out, "test:copymark", "copy roundtrip: copied row visible in new process");
    TEST_PASS("system: profile sql cross COPY roundtrip");
}

TEST_CASE("system: profile sql cross FORK persistence") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_fork_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p1 = "besq_fork_p1_" + unique_ts_suffix();
    const std::string p3 = "besq_fork_p3_" + unique_ts_suffix();
    {
        // p1 源含一行 test:src；FORK 派生 p3 = 全量克隆（含 vanilla tag 宇宙）
        std::ofstream f1(std::filesystem::path(tmp) / (p1 + ".json"));
        f1 << "{\"name\":\"" << p1 << "\",\"enchantments\":[{\"id\":\"test:src\",\"name\":\"Src\","
              "\"platform\":\"java\",\"max_level\":1,\"multiplier\":1,"
              "\"supported_items\":[\"#minecraft:swords\"]}],\"equipments\":[],\"tags\":{}}";
    }
    // 进程 1：FORK p1 AS p3; SAVE ALL（p3.json 落盘）
    auto r1 = run_besq(bin, {"profile", "sql", "FORK " + p1 + " AS " + p3 + "; SAVE ALL"}, {},
                       {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r1.exit_code, 0, "fork+save all: exit 0");
    expect_contains(r1.out, "forked: " + p3, "fork: message");
    expect_contains(r1.out, "saved: " + p3, "fork: save all message");
    // 进程 2（新进程）：profile list 见 p3 —— 派生持久化门
    auto r2 = run_besq(bin, {"profile", "list"}, {},
                       {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r2.exit_code, 0, "fork list: exit 0");
    expect_contains(r2.out, p3, "fork list: shows p3");
    // 进程 3（新进程）：profile info p3 可见
    auto r3 = run_besq(bin, {"profile", "info", p3}, {},
                       {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r3.exit_code, 0, "fork info: exit 0");
    expect_contains(r3.out, p3, "fork info: shows p3");
    TEST_PASS("system: profile sql cross FORK persistence");
}

TEST_CASE("system: profile sql cross MERGE status dirty") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_merge_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p1 = "besq_merge_p1_" + unique_ts_suffix();
    const std::string p2 = "besq_merge_p2_" + unique_ts_suffix();
    {
        std::ofstream f1(std::filesystem::path(tmp) / (p1 + ".json"));
        f1 << "{\"name\":\"" << p1 << "\",\"enchantments\":[],\"equipments\":[],\"tags\":{}}";
        std::ofstream f2(std::filesystem::path(tmp) / (p2 + ".json"));
        f2 << "{\"name\":\"" << p2 << "\",\"enchantments\":[{\"id\":\"test:mergesrc\",\"name\":\"MergeSrc\","
              "\"platform\":\"java\",\"max_level\":1,\"multiplier\":1,"
              "\"supported_items\":[\"#minecraft:swords\"]}],\"equipments\":[],\"tags\":{}}";
    }
    // 链：MERGE INTO p1 FROM p2 → STATUS p1 输出含目标脏标记 + 合并行 diff
    const std::string stmt = "MERGE INTO " + p1 + " FROM " + p2 + "; STATUS " + p1;
    auto r = run_besq(bin, {"profile", "sql", stmt}, {},
                      {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r.exit_code, 0, "merge+status: exit 0");
    expect_contains(r.out, "merged:", "merge: message prefix");
    expect_contains(r.out, "profile: " + p1 + " (dirty)", "merge: STATUS marks dest dirty");
    expect_contains(r.out, "+test:mergesrc", "merge: STATUS shows merged row");
    TEST_PASS("system: profile sql cross MERGE status dirty");
}

TEST_CASE("system: profile sql cross json messages") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_json_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p3 = "besq_json_fork_" + unique_ts_suffix();
    // --format json：FORK + COPY(WITH OVERRIDE) + SELECT；语句消息（forked: /
    // row(s) affected）走 stderr，stdout 保持纯 JSON（[表头, 行...]）
    const std::string stmt = "FORK builtin:vanilla AS " + p3 + "; "
        "COPY * FROM builtin:vanilla INTO enchantment WHERE id='minecraft:sharpness' WITH OVERRIDE; "
        "SELECT id FROM enchantment WHERE id='minecraft:sharpness';";
    auto r = run_besq(bin, {"profile", "sql", stmt, "--format", "json"}, {},
                      {{"BESQ_PROFILES_DIR", tmp}, {"BESQ_LANG", "en_US"}});
    expect_eq(r.exit_code, 0, "cross json: exit 0");
    expect(!r.out.empty() && r.out.front() == '[', "cross json: stdout is a JSON array");
    expect_contains(r.out, "minecraft:sharpness", "cross json: SELECT row in JSON");
    expect(r.out.find("forked") == std::string::npos, "cross json: no message text on stdout");
    expect(r.out.find("row(s) affected") == std::string::npos, "cross json: no affected msg on stdout");
    expect_contains(r.err, "forked: " + p3, "cross json: forked message on stderr");
    expect_contains(r.err, "row(s) affected", "cross json: affected message on stderr");
    TEST_PASS("system: profile sql cross json messages");
}

// ── slice-3 REPL system e2e（真实 CLI：profile sql -i + 管道 stdin）────────
//
// 全部经 run_besq 的 stdin_input 管道注入；退出码由 popen/CreateProcess 原生
// 捕获（非 shell $?）。交互 UI（提示符 `profile> `/`...> `、HELP、undo 确认）
// text → stdout、json → stderr；语句消息 text → stdout、json → stderr；错误
// （语句错误/解析错误）恒 stderr。临时 profiles 目录（BESQ_PROFILES_DIR）隔离
// + unique_ts_suffix 唯一 profile 名，RAII 清理，顺序无关。

TEST_CASE("system: profile sql repl basic") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // REPL 会话内 USE p2 → INSERT → SELECT（跨语句常驻会话），QUIT 带脏警告
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_repl_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p2 = "besq_repl_p2_" + unique_ts_suffix();
    const std::string x = "test:replmark";
    {
        std::ofstream f(std::filesystem::path(tmp) / (p2 + ".json"));
        f << "{\"name\":\"" << p2 << "\",\"enchantments\":[],\"equipments\":[],\"tags\":{}}";
    }
    const std::string stdin_input =
        "USE " + p2 + ";\n"
        "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
        "VALUES ('" + x + "','ReplMark',1,1,'#minecraft:swords');\n"
        "SELECT id FROM enchantment WHERE id='" + x + "';\n"
        "QUIT\n";
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"}, stdin_input,
                      {{"BESQ_PROFILES_DIR", tmp}});
    expect_eq(r.exit_code, 0, "repl basic: exit 0");
    expect_contains(r.out, "profile> ", "repl basic: interactive prompt");
    expect_contains(r.out, "use: " + p2, "repl basic: USE message");
    expect_contains(r.out, "1 row(s) affected", "repl basic: insert message");
    expect_contains(r.out, x, "repl basic: SELECT sees the inserted row");
    expect_contains(r.err, "unsaved changes in: " + p2, "repl basic: dirty tracks p2 on exit");
    TEST_PASS("system: profile sql repl basic");
}

TEST_CASE("system: profile sql repl omitted semicolon") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // 单行省略分号：parse 成功即提交（无 ';' 也可）
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                      "SELECT id FROM enchantment WHERE id='minecraft:sharpness'\nQUIT\n", {});
    expect_eq(r.exit_code, 0, "repl no-semicolon: exit 0");
    expect_contains(r.out, "minecraft:sharpness", "repl no-semicolon: single-line submit shows row");
    TEST_PASS("system: profile sql repl omitted semicolon");
}

TEST_CASE("system: profile sql repl multiline continuation") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // 多行续行：SELECT id, + 换行 + name → 续行提示 ...> 出现，两列结果
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                      "SELECT id,\n name FROM enchantment;\nQUIT\n", {});
    expect_eq(r.exit_code, 0, "repl multiline: exit 0");
    expect_contains(r.out, "...> ", "repl multiline: continuation prompt");
    expect_contains(r.out, "name", "repl multiline: second column rendered");
    TEST_PASS("system: profile sql repl multiline continuation");
}

TEST_CASE("system: profile sql repl error does not exit") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // FROB 报错后 SELECT 仍执行（错误不退出 REPL）
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                      "FROB x;\nSELECT id FROM enchantment WHERE id='minecraft:sharpness';\nQUIT\n", {});
    expect_eq(r.exit_code, 0, "repl err-continue: exit 0");
    expect_contains(r.err, "unsupported statement 'frob'", "repl err-continue: FROB parse error on stderr");
    expect_contains(r.out, "minecraft:sharpness", "repl err-continue: SELECT still executed after error");
    TEST_PASS("system: profile sql repl error does not exit");
}

TEST_CASE("system: profile sql repl hard error vs continuation") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // (a) UPDATE 无 WHERE 且无分号 → 提交期硬错误（requires WHERE），清缓冲
    //     不续行（后续提示是 profile> 而非 ...>）
    auto ra = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                       "UPDATE equipment SET max_durability=500\nQUIT\n", {});
    expect_eq(ra.exit_code, 0, "repl hard: exit 0");
    expect_contains(ra.err, "requires WHERE", "repl hard: UPDATE requires WHERE on stderr");
    expect(ra.out.find("...> ") == std::string::npos, "repl hard: no continuation prompt after hard error");
    // (b) SELECT FROM; → 提交期解析硬错误（expected FROM），不崩溃
    auto rb = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                       "SELECT FROM;\nQUIT\n", {});
    expect_eq(rb.exit_code, 0, "repl hard select-from: exit 0");
    expect_contains(rb.err, "expected FROM", "repl hard: SELECT FROM parse error on stderr");
    // (c) SELECT FROM（无分号）→ 续行 ...>，QUIT 后 EOF 残余缓冲报错，不崩溃
    auto rc_ = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                        "SELECT FROM\nQUIT\n", {});
    expect_eq(rc_.exit_code, 0, "repl hard select-from-eof: exit 0");
    expect_contains(rc_.out, "...> ", "repl hard select-from-eof: continuation prompt shown");
    expect_contains(rc_.err, "expected FROM", "repl hard select-from-eof: EOF residual error on stderr");
    TEST_PASS("system: profile sql repl hard error vs continuation");
}

TEST_CASE("system: profile sql repl illegal char hard error") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // 非法字符 '&' → lexer 错误（lexer error: unexpected character '&'）必须硬
    // 错误：立即 stderr 报错 + 清缓冲，绝不续行（无 ...> 提示）；QUIT 正常退出
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"},
                      "SELECT id FROM enchantment WHERE id='minecraft:sharpness' &\nQUIT\n", {});
    expect_eq(r.exit_code, 0, "repl illegal: exit 0");
    expect_contains(r.err, "unexpected character", "repl illegal: lexer error on stderr");
    expect(r.out.find("...> ") == std::string::npos, "repl illegal: no continuation prompt (hard error)");
    TEST_PASS("system: profile sql repl illegal char hard error");
}

TEST_CASE("system: profile sql repl help") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // HELP：语句清单（text 模式 → stdout）
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"}, "HELP\nQUIT\n", {});
    expect_eq(r.exit_code, 0, "repl help: exit 0");
    expect_contains(r.out, "SELECT", "repl help: statement list mentions SELECT");
    expect_contains(r.out, "FORK", "repl help: statement list mentions FORK");
    expect_contains(r.out, "UNDO", "repl help: statement list mentions UNDO");
    TEST_PASS("system: profile sql repl help");
}

TEST_CASE("system: profile sql repl cross undo") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // 跨调用 UNDO：INSERT（行写入）→ UNDO 命令（undo: reverted）→ SELECT 不见行
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_repl_undo_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string x = "test:replundo";
    const std::string stdin_input =
        "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
        "VALUES ('" + x + "','ReplUndo',1,1,'#minecraft:swords');\n"
        "UNDO;\n"
        "SELECT id FROM enchantment WHERE id='" + x + "';\n"
        "QUIT\n";
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"}, stdin_input,
                      {{"BESQ_PROFILES_DIR", tmp}});
    expect_eq(r.exit_code, 0, "repl undo: exit 0");
    expect_contains(r.out, "undo: reverted", "repl undo: confirmation message");
    expect(r.out.find(x) == std::string::npos, "repl undo: row not visible after UNDO");
    TEST_PASS("system: profile sql repl cross undo");
}

TEST_CASE("system: profile sql repl dirty warning") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // 脏警告退出：USE p2 → INSERT（未 SAVE）→ QUIT → stderr 点名 p2，exit 0
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_repl_dirty_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string p2 = "besq_repl_dirty_p2_" + unique_ts_suffix();
    {
        std::ofstream f(std::filesystem::path(tmp) / (p2 + ".json"));
        f << "{\"name\":\"" << p2 << "\",\"enchantments\":[],\"equipments\":[],\"tags\":{}}";
    }
    const std::string stdin_input =
        "USE " + p2 + ";\n"
        "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
        "VALUES ('test:dirtymark','DirtyMark',1,1,'#minecraft:swords');\n"
        "QUIT\n";
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i"}, stdin_input,
                      {{"BESQ_PROFILES_DIR", tmp}});
    expect_eq(r.exit_code, 0, "repl dirty: exit 0");
    expect_contains(r.out, "use: " + p2, "repl dirty: USE message");
    expect_contains(r.err, "unsaved changes in: " + p2, "repl dirty: warning names p2");
    TEST_PASS("system: profile sql repl dirty warning");
}

TEST_CASE("system: profile sql repl json format") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // --format json：stdout 纯 JSON 数组（提示符/语句消息全走 stderr）
    const std::string tmp = (std::filesystem::temp_directory_path() / ("besq_sql_repl_json_" + unique_ts_suffix())).string();
    struct Guard { std::string p; ~Guard() { std::error_code ec; std::filesystem::remove_all(p, ec); } } guard{tmp};
    std::filesystem::create_directories(tmp);
    const std::string x = "test:repljson";
    const std::string stdin_input =
        "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
        "VALUES ('" + x + "','ReplJson',1,1,'#minecraft:swords');\n"
        "SELECT id FROM enchantment WHERE id='" + x + "';\n"
        "QUIT\n";
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i", "--format", "json"}, stdin_input,
                      {{"BESQ_PROFILES_DIR", tmp}});
    expect_eq(r.exit_code, 0, "repl json: exit 0");
    expect(!r.out.empty() && r.out.front() == '[', "repl json: stdout starts with a JSON array");
    expect_contains(r.out, x, "repl json: SELECT row in JSON");
    expect(r.out.find("row(s) affected") == std::string::npos, "repl json: no message text on stdout");
    expect(r.out.find("profile> ") == std::string::npos, "repl json: no prompt on stdout");
    expect_contains(r.err, "row(s) affected", "repl json: affected message on stderr");
    TEST_PASS("system: profile sql repl json format");
}

TEST_CASE("system: profile sql repl interactive exclusive") {
    const std::string bin = find_besq();
    if (bin.empty()) {
        SKIP("besq binary not found (build with BESQ_BUILD_CLI=ON or set "
             "BESQ_BIN_PATH)");
    }
    // -i 与 <stmt> 互斥 → 本地化错误，exit 1（--lang en_US 钉英文文案）
    auto r = run_besq(bin, {"--lang", "en_US", "profile", "sql", "-i", "SELECT 1"}, {}, {});
    expect_eq(r.exit_code, 1, "repl exclusive: exit 1");
    expect_contains(r.err, "-i cannot be combined with a statement argument",
                    "repl exclusive: mutual-exclusion error on stderr");
    TEST_PASS("system: profile sql repl interactive exclusive");
}
