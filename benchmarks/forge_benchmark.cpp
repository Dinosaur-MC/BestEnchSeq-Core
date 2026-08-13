#include "framework/bench_framework.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/types/Enchantment.h"
#include "common/io/json.h"
#include "log/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ─── Data structures (dynamically loaded from disk) ─────────────────────

struct TestCase {
    std::string name;
    std::string item_type;
    std::vector<std::string> wanted;
    int max_cost;
};

struct TestGroup {
    std::string name;
    std::vector<TestCase> cases;
};

// ─── Global state (loaded once at startup) ─────────────────────────────

static std::unordered_map<std::string, Profile> g_profiles;
static std::vector<TestGroup> g_groups;

// ─── Profile loading ──────────────────────────────────────────────────

void load_profiles(const std::filesystem::path& dir) {
    ProfileLoader loader;

    if (std::filesystem::is_directory(dir)) {
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() != ".json") continue;
            try {
                Profile p = loader.load(entry.path());
                // Profile keys are plain strings (B-T13) — use the name verbatim.
                std::string name = p.name();
                if (name.empty()) name = entry.path().stem().string();
                g_profiles[name] = std::move(p);
                std::cout << "  Profile: " << name << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "  WARN: failed to load profile '"
                          << entry.path().filename() << "': " << e.what() << std::endl;
            }
        }
    }

    // Fallback: builtin data if no profiles loaded
    if (g_profiles.empty()) {
        std::cout << "  Profile: (builtin)" << std::endl;
        Profile p = loader.load_builtin();
        g_profiles[p.name()] = std::move(p);
    }
}

// ─── Profile merging ──────────────────────────────────────────────────

/// Merge multiple profiles into one.  The first profile is the base;
/// subsequent profiles are merged into it via ProfileManager::merge().
Profile merge_profiles(const std::vector<std::string>& names) {
    if (names.empty())
        return {};

    // Clone first profile as the base.
    auto base_it = g_profiles.find(names[0]);
    if (base_it == g_profiles.end())
        return {};
    Profile merged = base_it->second.clone("__merged__");

    // Merge subsequent profiles directly via Profile mutation methods.
    for (size_t i = 1; i < names.size(); ++i) {
        auto it = g_profiles.find(names[i]);
        if (it == g_profiles.end()) {
            std::cerr << "  WARN: profile '" << names[i] << "' not found, skipping\n";
            continue;
        }
        const auto& src = it->second;

        for (const auto& [nsid, ench] : src.ench().data()) {
            if (merged.ench().contains(nsid))
                merged.update_enchantment(ench);
            else
                merged.add_enchantment(ench);
        }
        for (const auto& [id, eq] : src.eq().data()) {
            if (!merged.eq().contains(id))
                merged.add_equipment(eq);
        }
        for (const auto& [nsid, tag] : src.tags().data()) {
            if (!merged.tags().contains(nsid))
                merged.add_tag(tag);
        }
    }

    return merged;
}

// ─── Test case loading ────────────────────────────────────────────────

void load_testcases(const std::filesystem::path& dir) {
    if (!std::filesystem::is_directory(dir)) {
        std::cerr << "  WARN: testcases directory '" << dir << "' not found\n";
        return;
    }

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream ifs(entry.path());
            Json json = Json::parse(ifs);

            // Resolve profile(s) — either single "profile" or "profiles" array
            std::vector<std::string> profile_names;
            if (json.has("profiles") && json["profiles"].type() == JsonType::Array) {
                for (auto& jp : json["profiles"].as_array())
                    profile_names.push_back(jp.as_string());
            } else if (json.has("profile")) {
                profile_names.push_back(json["profile"].as_string());
            } else {
                profile_names.push_back("builtin:vanilla");  // sensible default
            }

            for (auto& jgroup : json["groups"].as_array()) {
                TestGroup group;
                group.name = jgroup["group"].as_string();
                group.cases.reserve(jgroup["cases"].as_array().size());

                for (auto& jcase : jgroup["cases"].as_array()) {
                    TestCase tc;
                    tc.name      = jcase["name"].as_string();
                    tc.item_type = jcase["item_type"].as_string();
                    tc.max_cost  = static_cast<int>(jcase["max_cost"].as_int());
                    for (auto& w : jcase["wanted"].as_array())
                        tc.wanted.push_back(w.as_string());
                    group.cases.push_back(std::move(tc));
                }

                // Stash profile names on the group metadata for run_case
                // by appending them into the group name for now.
                group.name += "|";
                for (size_t i = 0; i < profile_names.size(); ++i) {
                    if (i > 0) group.name += "+";
                    group.name += profile_names[i];
                }

                // Sort cases by enchantment count (ascending)
                std::sort(group.cases.begin(), group.cases.end(),
                    [](const TestCase& a, const TestCase& b) {
                        return a.wanted.size() < b.wanted.size();
                    });

                g_groups.push_back(std::move(group));
            }
        } catch (const std::exception& e) {
            std::cerr << "  WARN: failed to load testcases from '"
                      << entry.path().filename() << "': " << e.what() << std::endl;
        }
    }
}

// ─── Parse profile names from group (stored after '|') ────────────────

static std::vector<std::string> split_profiles(const std::string& group_name) {
    auto p = group_name.find('|');
    if (p == std::string::npos)
        return {"builtin:vanilla"};
    std::string rest = group_name.substr(p + 1);
    std::vector<std::string> names;
    std::istringstream ss(rest);
    for (std::string tok; std::getline(ss, tok, '+');)
        if (!tok.empty()) names.push_back(tok);
    return names;
}

// ─── Dynamic tier matrix (generated from each algorithm's evaluate()) ──
//
// Replaces the old hand-maintained ALGO_LIMITS table.  Tier T means
// "single-case time budget = 10^T ms" (T=0 → 1 ms … T=8 → 100 s).  An
// algorithm's cell at tier T = the largest enchant count e whose predicted
// time evaluate(e) fits the budget, so the matrix is regenerated from the
// loaded algorithms' fitted evaluate() curves.
//
//   tier→   0    1    2    3    4    5    6    7    8
//   budget  1ms  10ms 100ms 1s   10s  100s 1e3s 1e4s 1e5s

static constexpr int TIER_LEVELS = 9;    // 0..8
static constexpr int DEFAULT_TIER = 3;   // 1 s budget
static constexpr int TIER_MAX_ENCH = 40; // matrix scan ceiling

/// Tier time budget in seconds == 10^(T-3) == 10^T ms.
static double tier_budget_sec(int tier) {
    return std::pow(10.0, static_cast<double>(tier - 3));
}

/// Predicted wall time (seconds) for \p algo at \p ench_count.
static double predicted_sec(const algorithm::IAlgorithm& algo, int ench_count) {
    return algo.evaluate(static_cast<int16_t>(ench_count));
}

/// Largest e in [0, TIER_MAX_ENCH] whose predicted time fits the tier budget
/// (evaluate() is monotonic in the enchant count → binary search).
static int max_ench_for_tier(const algorithm::IAlgorithm& algo, int tier) {
    double budget = tier_budget_sec(tier);
    int lo = 0, hi = TIER_MAX_ENCH;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (predicted_sec(algo, mid) <= budget) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/// Check whether \p algo should be skipped for a case with \p ench_count at
/// \p tier (predicted time exceeds the tier budget).  False when \p no_skip.
static bool should_skip(const algorithm::IAlgorithm& algo, int ench_count,
                        int tier, bool no_skip) {
    if (no_skip) return false;
    return predicted_sec(algo, ench_count) > tier_budget_sec(tier);
}

/// Print the dynamically generated tier matrix for all loaded algorithms.
static void print_algo_limits(const algorithm::AlgorithmLoader& loader) {
    std::cout << "\nDynamic tier matrix (tier T = 10^T ms budget; from evaluate()):\n";
    std::cout << "  " << std::left << std::setw(20) << "Algorithm";
    for (int t = 0; t < TIER_LEVELS; ++t)
        std::cout << std::right << std::setw(4) << t;
    std::cout << "\n";
    std::cout << "  " << std::string(4 * TIER_LEVELS + 20, '-') << "\n";
    for (const auto& name : loader.list()) {
        auto algo = loader.create(name);
        if (!algo) continue;
        std::cout << "  " << std::left << std::setw(20) << name;
        for (int t = 0; t < TIER_LEVELS; ++t)
            std::cout << std::right << std::setw(4) << max_ench_for_tier(*algo, t);
        std::cout << "\n";
    }
    std::cout << "  (default tier " << DEFAULT_TIER << " = "
              << tier_budget_sec(DEFAULT_TIER) << " s budget. "
              << "--no-skip disables all limits.)\n";
}

// ─── Helper: comma-separated list → unordered_set ───
static std::unordered_set<std::string> split_csv(const std::string& s) {
    std::unordered_set<std::string> out;
    std::istringstream ss(s);
    for (std::string tok; std::getline(ss, tok, ',');)
        if (!tok.empty()) out.insert(tok);
    return out;
}

// ─── CLI flag parsing ──────────────────────────────────────────────────

struct BenchConfig {
    std::unordered_set<std::string> test_names; // empty = all
    std::unordered_set<std::string> raw_algos;  // from --alg; empty = all loaded
    std::string algo_dir;                       // plugin dir, empty = none
    int tier = DEFAULT_TIER;                    // 0..6 permissiveness level
    int max_time_sec = 600;                     // per-algorithm search budget (seconds)
    int iterations = 1;                         // 每次算法运行重复次数（harness 计时；1 = 单次，行为同旧版）
    int warmup = 1;                             // harness warmup 次数
    bool json = false;                          // 机器可解析输出（opt-in；bench_report 不受影响）
    bool list_only = false;
    bool no_skip = false;
};

BenchConfig parse_cli(int argc, char* argv[]) {
    BenchConfig cfg;

    auto die = [](const std::string& msg) {
        std::cerr << "Error: " << msg << "\n"
                  << "Usage: forge_benchmark [options]\n"
                  << "  --list                List test cases, groups & algorithm limits\n"
                  << "  --test  <names>       Comma-separated test names\n"
                  << "  --group <names>       Comma-separated group names\n"
                  << "  --alg   <names>       Comma-separated algorithm names (--algo also accepted)\n"
                  << "  --algo-dir <dir>      Load algorithm plugins from directory\n"
                  << "  --tier <num>          Algorithm permissiveness level (0-" << TIER_LEVELS - 1 << ", default " << DEFAULT_TIER << ")\n"
                  << "  --no-skip             Run all algorithms without enchant limits\n"
                  << "  --iterations <num>    Repeat each algorithm run for wall-time stats (default 1)\n"
                  << "  --warmup <num>        Harness warmup iterations (default 1)\n"
                  << "  --json                Machine-readable JSON summary (after === Done ===)\n"
                  << "  --help                This help" << std::endl;
        std::exit(1);
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            cfg.list_only = true;
        } else if (arg == "--test") {
            if (i + 1 >= argc) die("--test requires a value");
            cfg.test_names.clear();
            cfg.test_names = split_csv(argv[++i]);
        } else if (arg == "--group") {
            if (i + 1 >= argc) die("--group requires a value");
            cfg.test_names.clear();
            for (const auto& tok : split_csv(argv[++i])) {
                bool found = false;
                for (const auto& g : g_groups) {
                    // Compare just the display name (before '|')
                    auto display = g.name.substr(0, g.name.find('|'));
                    if (tok == display) {
                        for (const auto& tc : g.cases)
                            cfg.test_names.insert(tc.name);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    die("unknown group '" + tok + "'");
            }
        } else if (arg == "--algo" || arg == "--alg") {
            if (i + 1 >= argc) die("--alg requires a value");
            cfg.raw_algos = split_csv(argv[++i]);
        } else if (arg == "--algo-dir") {
            if (i + 1 >= argc) die("--algo-dir requires a value");
            cfg.algo_dir = argv[++i];
        } else if (arg == "--tier") {
            if (i + 1 >= argc) die("--tier requires a value (0-" + std::to_string(TIER_LEVELS - 1) + ")");
            int t = std::atoi(argv[++i]);
            if (t < 0 || t > TIER_LEVELS - 1) die("--tier must be 0-" + std::to_string(TIER_LEVELS - 1));
            cfg.tier = t;
        } else if (arg == "--no-skip") {
            cfg.no_skip = true;
        } else if (arg == "--max-time") {
            if (i + 1 >= argc) die("--max-time requires a value (seconds)");
            int s = std::atoi(argv[++i]);
            if (s <= 0) die("--max-time must be a positive integer (seconds)");
            cfg.max_time_sec = s;
        } else if (arg == "--iterations") {
            if (i + 1 >= argc) die("--iterations requires a value");
            int n = std::atoi(argv[++i]);
            if (n < 1) die("--iterations must be a positive integer");
            cfg.iterations = n;
        } else if (arg == "--warmup") {
            if (i + 1 >= argc) die("--warmup requires a value");
            int n = std::atoi(argv[++i]);
            if (n < 0) die("--warmup must be >= 0");
            cfg.warmup = n;
        } else if (arg == "--json") {
            cfg.json = true;
        } else if (arg == "--help") {
            std::cout << "Usage: forge_benchmark [options]\n"
                      << "  --list                List test cases, groups & algorithm limits\n"
                      << "  --test  <names>       Comma-separated test names\n"
                      << "  --group <names>       Comma-separated group names\n"
                      << "  --alg   <names>       Comma-separated algorithm names (--algo also accepted)\n"
                      << "  --algo-dir <dir>      Load algorithm plugins from directory\n"
                      << "  --tier <num>          Algorithm permissiveness level (0-" << TIER_LEVELS - 1 << ", default " << DEFAULT_TIER << ")\n"
                      << "  --max-time <secs>     Per-algorithm search time budget (default 600s)\n"
                      << "  --no-skip             Run all algorithms without enchant limits\n"
                      << "  --help                This help\n";
            std::cout << "\nDynamic tier matrix (see --list): tier T = 10^T ms "
                      << "budget, regenerated from each loaded algorithm's\n"
                      << "  evaluate() curve.  Algorithms whose predicted runtime "
                      << "exceeds the tier budget are SKIPped\n"
                      << "  (--no-skip disables).  Default tier " << DEFAULT_TIER
                      << " = " << tier_budget_sec(DEFAULT_TIER) << " s.\n";
            std::cout << "\nExamples:\n"
                      << "  forge_benchmark --group netherite\n"
                      << "  forge_benchmark --test netherite_sword,boots_full --alg dp_merge\n"
                      << "  forge_benchmark --group armor --alg hamming\n"
                      << "  forge_benchmark --algo-dir build/plugins --group sword\n"
                      << "  forge_benchmark --group large\n";
            std::exit(0);
        } else if (arg.size() > 1 && arg[0] == '-') {
            die("unknown flag '" + arg + "'");
        }
    }

    // Validate: --test names must exist
    if (!cfg.test_names.empty()) {
        std::unordered_set<std::string> valid_tests;
        for (const auto& g : g_groups)
            for (const auto& tc : g.cases)
                valid_tests.insert(tc.name);
        for (const auto& t : cfg.test_names)
            if (!valid_tests.contains(t))
                die("unknown test '" + t + "'");
    }

    return cfg;
}

// ─── Resolve algorithm list after loader is ready ─────────────────────

static std::vector<std::string>
resolve_algos(const BenchConfig& cfg, const algorithm::AlgorithmLoader& loader) {
    std::vector<std::string> all = loader.list();
    std::sort(all.begin(), all.end());

    if (cfg.raw_algos.empty())
        return all;

    std::vector<std::string> filtered;
    for (const auto& name : cfg.raw_algos) {
        auto plus = name.find('+');
        if (plus != std::string::npos) {
            std::string w = name.substr(0, plus);
            std::string m = name.substr(plus + 1);
            if (!loader.contains(w) || !loader.contains(m)) {
                std::cerr << "  WARN: chain '" << name << "' requires '"
                          << w << "' and '" << m
                          << "', but not all are loaded — skipping\n";
                continue;
            }
            filtered.push_back(name);
        } else {
            if (loader.contains(name)) {
                filtered.push_back(name);
            } else {
                std::cerr << "  WARN: algorithm '" << name
                          << "' is not loaded — skipping\n";
            }
        }
    }
    std::sort(filtered.begin(), filtered.end());
    return filtered;
}

// ─── Run a single test case against every <algos> entry ───────────────

/// Print a benchmark result row, verifying the final item meets the target
/// enchantments (empty steps = GoalAlreadyMet, trivially met).
void print_result(const algorithm::AlgorithmOutput& out,
                  const algorithm::Item& target, int32_t max_cost) {
    if (out.solutions.empty()) {
        std::cout << "no solution" << std::endl;
        return;
    }
    int32_t total = out.solutions[0].total_cost;
    bool meets = out.solutions[0].steps.empty()
        || meets_target(out.final_item, target);
    bool ok = total <= max_cost && meets;
    if (!meets)
        std::cout << "  !! MISSING TARGET ENCHANTMENTS" << std::endl;
    std::cout << std::right << std::setw(4) << total << "L"
              << (ok ? "  \xe2\x9c\x85" : "  \xe2\x9a\xa0")
              << "  " << std::setw(4) << out.computation_time.count()
              << "ms" << std::endl;
}

/// 一次算法运行的结果状态（JSON status 全集合，spec 2026-08-07-bench-report-json）。
/// 映射依据：AlgorithmState 无 TimedOut 态，超时监视器调 cancel() →
/// Cancelled 即 --max-time 预算超时（forge 语境无外部取消）。
enum class RunStatus { Ok, Skip, NoSolution, Timeout, Failed };

inline const char* status_name(RunStatus s) {
    switch (s) {
        case RunStatus::Ok: return "ok";
        case RunStatus::Skip: return "skip";
        case RunStatus::NoSolution: return "no-solution";
        case RunStatus::Timeout: return "timeout";
        case RunStatus::Failed: return "failed";
    }
    return "failed";
}

/// 多次运行的 Executor 内置 computation_time 中位数（用户指示：以 Executor
/// 计时为基准指标，2026-08-07）。
inline int64_t comp_median_ms(std::vector<int64_t>& comps) {
    std::sort(comps.begin(), comps.end());
    return comps[comps.size() / 2];
}

/// 单次算法运行的完整记录（JSON 输出行；文本输出行保持字节兼容——
/// bench_report.py 的回退解析只认 ✅/SKIP/no solution 行，note 行不与其
/// 正则碰撞）。计时以 Executor 内置 computation_time 为基准（用户指示，
/// 2026-08-07）：wall-clock 含线程 spawn/join 开销，非算法真实耗时。
struct JsonRow {
    std::string dataset;    // 展示名（bench_report 回退解析的 dataset key 同源）
    int ench_count = 0;
    int max_lvl = 0;
    std::string algo;
    RunStatus status = RunStatus::Failed;
    int L = 0;              // ok 时的总成本（print_result 同源）
    int64_t comp_ms = 0;    // ok 时的 Executor 内置计算时间（最后一次迭代）
    std::string note;       // skip 原因 / failed 错误消息
};

/// 单次算法执行：创建实例 → start → wait → 状态映射 → 输出。每次调用创建
/// 全新实例（迭代计时场景每轮独立）。
struct AlgoRunResult {
    RunStatus status = RunStatus::Failed;
    std::optional<algorithm::AlgorithmOutput> out;
};

AlgoRunResult run_algo_once(const algorithm::AlgorithmLoader& loader,
                            const std::string& algo_name,
                            const algorithm::AlgorithmInput& input) {
    auto algo = loader.create(algo_name);
    if (!algo)
        return {RunStatus::Failed, std::nullopt};
    algorithm::AlgorithmExecutor executor(std::move(algo));
    algorithm::AlgorithmInput ri = input;
    executor.start(std::move(ri));
    executor.wait();
    switch (executor.state()) {
        case algorithm::AlgorithmState::Completed: {
            auto out = executor.output();
            if (out.solutions.empty())
                return {RunStatus::NoSolution, std::nullopt};
            return {RunStatus::Ok, std::move(out)};
        }
        case algorithm::AlgorithmState::Cancelled:
            // 超时监视器取消（预算超时）
            return {RunStatus::Timeout, std::nullopt};
        default:
            return {RunStatus::Failed, std::nullopt};
    }
}

void run_case(const TestCase& tc, const Profile& profile,
              const std::vector<std::string>& algos,
              const algorithm::AlgorithmLoader& loader,
              int tier, bool no_skip, int max_time_sec,
              int iterations, int warmup, std::vector<JsonRow>& rows) {
    // 数据集元信息（JSON 结构化：name 只写用例名，ench_count/max_lvl 独立
    // 字段；bench_report JSON 路径据此重建展示名供下游聚合）。
    const std::string& ds_display = tc.name;
    const auto& G_ENCH = profile.ench();
    const auto& G_EQ   = profile.eq();

    auto eq_it = G_EQ.find(NSID(tc.item_type));
    if (eq_it == G_EQ.end()) {
        std::cout << "  [SKIP] unknown equipment '" << tc.item_type << "'\n";
        return;
    }
    const Equipment& eq = *eq_it;

    // Applicability uses the real MC tag-membership model (T10): an enchant
    // applies to the equipment iff the equipment's tag set intersects the
    // enchant's `#tag` supported_items refs, or the equipment id is itself a
    // concrete supported item.  Prefer the profile's attached resolver; fall
    // back to a category-derived resolver for resolver-less profiles.
    const TagResolver* tag_resolver = profile.tag_resolver();
    TagResolver category_fallback;
    if (!tag_resolver) {
        std::unordered_map<std::string, std::unordered_set<std::string>> members;
        for (const auto& [id, e] : profile.eq().data())
            if (e.category.is_tag())
                members[e.category.str().substr(1)].insert(id.str());
        for (const auto& [k, v] : members)
            category_fallback.add_tag(k, v);
        tag_resolver = &category_fallback;
    }
    const std::unordered_set<NSID> eq_tags = tag_resolver->tags_of(eq.id.str());

    // ── Parse wanted enchants + build business items ───────────────────
    ::EnchSet wanted_set;
    for (const auto& spec : tc.wanted) {
        auto p = spec.find('=');
        std::string id = spec.substr(0, p);
        int32_t lv = std::stoi(spec.substr(p + 1));
        auto ench_it = G_ENCH.find(NSID(id));
        if (ench_it == G_ENCH.end()) {
            std::cout << "  [SKIP] unknown enchant '" << id << "'\n";
            return;
        }
        wanted_set.insert(Ench{ench_it->id, ench_it->name, lv});
    }

    // ── Build compact EnchReg for this equipment ───────────────────────
    algorithm::EnchReg ench_reg;
    {
        const auto& all_infos = G_ENCH.data();
        std::vector<std::pair<NSID, EnchInfo>> sorted(all_infos.begin(), all_infos.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<algorithm::EnchInfo> algo_infos;
        std::vector<NSID> global_ids;
        std::unordered_map<NSID, int16_t> nsid_to_local;

        for (int32_t gid = 0; gid < static_cast<int32_t>(sorted.size()); ++gid) {
            const auto& biz = sorted[gid].second;
            bool applicable = biz.supported_items.contains(eq.id)
                           || std::any_of(
                                  biz.supported_items.begin(),
                                  biz.supported_items.end(),
                                  [&](const NSID& t) {
                                      return t.is_tag() && eq_tags.contains(t);
                                  });
            if (!applicable) continue;

            algorithm::EnchInfo ai;
            ai.id      = static_cast<uint8_t>(algo_infos.size());
            ai.mul     = static_cast<uint8_t>(biz.multiplier);
            ai.mul_b   = static_cast<uint8_t>(std::max(1, biz.multiplier >> 1));
            ai.max_lvl = static_cast<uint8_t>(biz.max_level);
            ai.exc_mask = 0;
            for (size_t li = 0; li < algo_infos.size(); ++li) {
                if (biz.exclusive_set.count(global_ids[li])) {
                    ai.exc_mask |= (algorithm::mask_type(1) << li);
                }
            }

            ai.applicable = true;

            int16_t lid = static_cast<int16_t>(algo_infos.size());
            nsid_to_local[sorted[gid].first] = lid;
            global_ids.push_back(sorted[gid].first);
            algo_infos.push_back(std::move(ai));
        }

        algorithm::Equipment algo_equip;
        algo_equip.id             = NSID("builtin_benchmark_equip");
        algo_equip.max_durability = eq.max_durability;
        for (int16_t i = 0; i < static_cast<int16_t>(algo_infos.size()); ++i)
            algo_equip.applicable_enchs.insert(i);

        ench_reg.init(std::move(algo_infos), std::move(global_ids), algo_equip);
    }

    // ── Build AlgorithmInput via from_domain ───────────────────────────
    algorithm::AlgorithmInput algo_input;
    algo_input.config.forge.platform = MCE::Java;
    algo_input.config.search.max_search_time = std::chrono::seconds(max_time_sec);
    algo_input.registry = std::move(ench_reg);

    {
        algorithm::EnchSet target_enchs;
        for (const auto& e : wanted_set) {
            try {
                auto lid = algo_input.registry.to_local_id(e.id);
                target_enchs.insert(algorithm::Ench{lid,
                                                     static_cast<algorithm::Ench::value_type>(e.level)});
            } catch (const std::out_of_range &) {
                // enchantment not applicable — skip
            }
        }
        algo_input.target.enchs = std::move(target_enchs);
        algo_input.target.type  = algorithm::ItemType::Equip;
        algo_input.target.ppn   = 0;
        algo_input.target.dur   = static_cast<int16_t>(eq.max_durability);
    }
    algo_input.config.mode = AlgorithmMode::direct;
    // The working books are generated by the algorithm's resolver from the
    // (empty) direct source — one book per wanted enchant, matching the
    // former explicit `books` list.
    algo_input.data = algorithm::DirectPayload{};

    // ════════════════════════════════════════════════════════════════════
    // Iterate algos in sorted order
    // ════════════════════════════════════════════════════════════════════
    for (const auto& algo_name : algos) {
        // 前缀行立即 flush：算法运行可长达 --max-time 秒，管道缓冲下
        // 不 flush 则运行期间终端/文件零输出（实时反馈依赖此行先到）。
        std::cout << "  " << std::left << std::setw(18) << algo_name
                  << std::flush;

        auto plus = algo_name.find('+');
        if (plus != std::string::npos) {
            std::string warmup_name = algo_name.substr(0, plus);
            std::string main_name   = algo_name.substr(plus + 1);

            auto main_algo = loader.create(main_name);
            if (!main_algo) {
                std::cout << "no such algorithm" << std::endl;
                rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                                tc.max_cost, main_name, RunStatus::Failed, 0, 0,
                                "no such algorithm"});
                continue;
            }
            if (should_skip(*main_algo, static_cast<int>(tc.wanted.size()),
                            tier, no_skip)) {
                const std::string note = "SKIP (predicted " +
                    std::to_string(predicted_sec(*main_algo, tc.wanted.size())) +
                    "s > " + std::to_string(tier_budget_sec(tier)) + "s budget)";
                std::cout << note << std::endl;
                rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                                tc.max_cost, main_name, RunStatus::Skip, 0, 0,
                                note});
                continue;
            }
            try {
                // 链式（warmup+main）：计时以 Executor 内置 computation_time
                // 为基准（用户指示，2026-08-07——wall-clock 含线程 spawn/join
                // 开销，非算法真实耗时）。warmup 恒 0：算法确定性、首轮即预热。
                AlgoRunResult last;
                std::vector<int64_t> comps;  // 每次迭代的 Executor 计时（ms）
                bench::Case bc{main_name,
                               [&] {
                                   last = {RunStatus::Failed, std::nullopt};
                                   auto a = loader.create(main_name);
                                   if (!a)
                                       return;
                                   algorithm::AlgorithmExecutor ex(std::move(a));
                                   algorithm::AlgorithmInput ri = algo_input;
                                   ex.start(std::move(ri),
                                            loader.create(warmup_name));
                                   ex.wait();
                                   switch (ex.state()) {
                                       case algorithm::AlgorithmState::Completed: {
                                           auto o = ex.output();
                                           if (o.solutions.empty())
                                               last = {RunStatus::NoSolution, std::nullopt};
                                           else
                                               last = {RunStatus::Ok, std::move(o)};
                                           break;
                                       }
                                       case algorithm::AlgorithmState::Cancelled:
                                           last = {RunStatus::Timeout, std::nullopt};
                                           break;
                                       default:
                                           last = {RunStatus::Failed, std::nullopt};
                                           break;
                                   }
                                   if (last.status == RunStatus::Ok && last.out)
                                       comps.push_back(last.out->computation_time.count());
                               },
                               {}, {}, iterations};
                bench::run_case(bc, iterations, 0);
                // 先完成算法前缀行（bench_report 解析 `<algo> <L>L ✅ <ms>ms`
                // 整行结构）；iterations > 1 时追加 comp 中位数行（Executor
                // 计时聚合，不与报告正则碰撞）。
                int64_t comp_ms = 0;
                if (last.status == RunStatus::Ok && last.out &&
                    !last.out->solutions.empty()) {
                    comp_ms = last.out->computation_time.count();
                    print_result(*last.out, algo_input.target, tc.max_cost);
                } else {
                    std::cout << "no solution" << std::endl;
                }
                if (iterations > 1 && !comps.empty())
                    std::cout << "    " << main_name << " comp median "
                              << comp_median_ms(comps) << "ms (" << comps.size()
                              << " iters)" << std::endl;
                rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                                tc.max_cost, main_name, last.status,
                                last.out && !last.out->solutions.empty()
                                    ? last.out->solutions[0].total_cost
                                    : 0,
                                comp_ms, {}});
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << std::endl;
                rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                                tc.max_cost, main_name, RunStatus::Failed, 0, 0,
                                e.what()});
            }
            continue;
        }

        auto algo = loader.create(algo_name);
        if (!algo) {
            std::cout << "no such algorithm" << std::endl;
            rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                            tc.max_cost, algo_name, RunStatus::Failed, 0, 0,
                            "no such algorithm"});
            continue;
        }
        if (should_skip(*algo, static_cast<int>(tc.wanted.size()), tier, no_skip)) {
            const std::string note = "SKIP (predicted " +
                std::to_string(predicted_sec(*algo, tc.wanted.size())) +
                "s > " + std::to_string(tier_budget_sec(tier)) + "s budget)";
            std::cout << note << std::endl;
            rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                            tc.max_cost, algo_name, RunStatus::Skip, 0, 0,
                            note});
            continue;
        }
        try {
            // 计时以 Executor 内置 computation_time 为基准（用户指示，
            // 2026-08-07——wall-clock 含线程 spawn/join 开销，非算法真实
            // 耗时）。warmup 恒 0；iterations > 1 时聚合 comp 中位数。
            AlgoRunResult last;
            std::vector<int64_t> comps;  // 每次迭代的 Executor 计时（ms）
            bench::Case bc{algo_name,
                           [&] {
                               last = run_algo_once(loader, algo_name, algo_input);
                               if (last.status == RunStatus::Ok && last.out)
                                   comps.push_back(last.out->computation_time.count());
                           },
                           {}, {}, iterations};
            bench::run_case(bc, iterations, 0);
            // 先完成算法前缀行（bench_report 解析 `<algo> <L>L ✅ <ms>ms`
            // 整行结构）；iterations > 1 时追加 comp 中位数行（Executor
            // 计时聚合，不与报告正则碰撞）。
            int64_t comp_ms = 0;
            if (last.status == RunStatus::Ok && last.out &&
                !last.out->solutions.empty()) {
                comp_ms = last.out->computation_time.count();
                print_result(*last.out, algo_input.target, tc.max_cost);
            } else {
                std::cout << "no solution" << std::endl;
            }
            if (iterations > 1 && !comps.empty())
                std::cout << "    " << algo_name << " comp median "
                          << comp_median_ms(comps) << "ms (" << comps.size()
                          << " iters)" << std::endl;
            rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                            tc.max_cost, algo_name, last.status,
                            last.out && !last.out->solutions.empty()
                                ? last.out->solutions[0].total_cost
                                : 0,
                            comp_ms, {}});
        } catch (const std::exception& e) {
            std::cout << "ERROR: " << e.what() << std::endl;
            rows.push_back({ds_display, static_cast<int>(tc.wanted.size()),
                            tc.max_cost, algo_name, RunStatus::Failed, 0, 0,
                            e.what()});
        }
    }
}

void list_cases(const algorithm::AlgorithmLoader& loader) {
    int total = 0;
    for (const auto& g : g_groups) {
        auto display = g.name.substr(0, g.name.find('|'));
        std::cout << "  " << display << ":";
        for (const auto& tc : g.cases) {
            std::cout << " " << tc.name;
            ++total;
        }
        std::cout << "\n";
    }
    std::cout << "Total: " << total << " test cases\n";
    print_algo_limits(loader);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    Logger::instance().set_console_enabled(false);

    // ── Load profiles & test cases before CLI parsing (needed for group validation) ──
    // build type 程序自报（NDEBUG = 编译期既定事实，本工程仅 Debug/Release）。
    std::cout << "=== Dataset Benchmark ===\n";
#ifdef NDEBUG
    std::cout << "Build type: Release\n";
#else
    std::cout << "Build type: Debug\n";
#endif
    std::cout << "Loading profiles..." << std::endl;
    load_profiles("data/tests/profiles");

    std::cout << "Loading test cases..." << std::endl;
    load_testcases("data/tests/testcases");
    std::cout << std::endl;

    BenchConfig cfg = parse_cli(argc, argv);

    // Load algorithms before any branch — the dynamic tier matrix (--list)
    // and the skip checks both read evaluate() from loaded instances.
    algorithm::AlgorithmLoader loader;
    loader.load_builtin();

    if (!cfg.algo_dir.empty()) {
        std::filesystem::path dir(cfg.algo_dir);
        if (std::filesystem::is_directory(dir)) {
            size_t n = loader.scan_and_load(cfg.algo_dir);
            std::cout << "Loaded " << n << " plugin(s) from "
                      << cfg.algo_dir << std::endl;
        } else {
            std::cerr << "Warning: --algo-dir '" << cfg.algo_dir
                      << "' not found, skipping plugins" << std::endl;
        }
    }

    if (cfg.list_only) {
        list_cases(loader);
        return 0;
    }

    std::cout << "Time: "
              << std::chrono::current_zone()->to_local(
                     std::chrono::system_clock::now())
              << std::endl;

    std::vector<std::string> algos = resolve_algos(cfg, loader);
    if (algos.empty()) {
        std::cerr << "No algorithms available after filtering. "
                  << "Loaded " << loader.size() << " total." << std::endl;
        return 1;
    }

    std::cout << "Using " << algos.size() << " algorithm(s)"
              << " (" << loader.size() << " loaded)"
              << (cfg.algo_dir.empty() ? ". Use --algo-dir to load plugins."
                                       : ".")
              << std::endl;

    // ── Build test queue ──────────────────────────────────────────────
    // Keep merged profiles alive here (outlives the run loop below).
    struct QueuedCase {
        const TestCase* tc;
        const Profile*  profile;
    };
    std::vector<QueuedCase> queue;
    std::vector<Profile> merged_profiles;  // owns merged profile lifetimes

    for (const auto& g : g_groups) {
        auto profile_names = split_profiles(g.name);

        const Profile* resolved = nullptr;
        if (profile_names.size() == 1) {
            auto it = g_profiles.find(profile_names[0]);
            if (it != g_profiles.end())
                resolved = &it->second;
        } else {
            merged_profiles.push_back(merge_profiles(profile_names));
            if (!merged_profiles.back().name().empty())
                resolved = &merged_profiles.back();
        }

        if (!resolved) {
            std::cerr << "  WARN: profile(s) for group '"
                      << g.name.substr(0, g.name.find('|'))
                      << "' not available, skipping" << std::endl;
            continue;
        }

        for (const auto& tc : g.cases) {
            if (cfg.test_names.empty() || cfg.test_names.count(tc.name))
                queue.push_back({&tc, resolved});
        }
    }

    // Sort queue by enchantment count (ascending)
    std::sort(queue.begin(), queue.end(),
        [](const QueuedCase& a, const QueuedCase& b) {
            return a.tc->wanted.size() < b.tc->wanted.size();
        });

    if (queue.empty()) {
        std::cerr << "No matching test cases. Use --list to see available tests."
                  << std::endl;
        return 1;
    }

    // ── Run each test case ────────────────────────────────────────────
    std::vector<JsonRow> rows;
    for (auto& qc : queue) {
        std::cout << "\n" << qc.tc->name << " (" << qc.tc->wanted.size()
                  << " enchants, max " << qc.tc->max_cost << "L):" << std::endl;
        try {
            run_case(*qc.tc, *qc.profile, algos, loader, cfg.tier, cfg.no_skip,
                     cfg.max_time_sec, cfg.iterations, cfg.warmup, rows);
        } catch (const std::exception& e) {
            std::cerr << "  ERROR: " << e.what() << std::endl;
        }
    }

    std::cout << "\n=== Done ===" << std::endl;
    if (cfg.json) {
        // 机器可解析汇总（dataset 结构化；opt-in——bench_report.py 的回退
        // 解析止于 "=== Done ==="，此段不被其读取）。
        std::cout << "{\n  \"datasets\": [";
        bool first_ds = true;
        size_t i = 0;
        while (i < rows.size()) {
            const auto& ds = rows[i];
            std::cout << (first_ds ? "\n    " : ",\n    ")
                      << "{\"name\": \"" << ds.dataset << "\", \"ench_count\": "
                      << ds.ench_count << ", \"max_lvl\": " << ds.max_lvl
                      << ", \"results\": [";
            first_ds = false;
            bool first_r = true;
            while (i < rows.size() && rows[i].dataset == ds.dataset) {
                const auto& r = rows[i];
                std::cout << (first_r ? "\n      " : ",\n      ")
                          << "{\"algo\": \"" << r.algo << "\", \"status\": \""
                          << status_name(r.status) << "\"";
                if (r.status == RunStatus::Ok) {
                    std::cout << ", \"L\": " << r.L
                              << ", \"comp_ms\": " << r.comp_ms;
                }
                if (!r.note.empty())
                    std::cout << ", \"note\": \"" << r.note << "\"";
                std::cout << "}";
                first_r = false;
                ++i;
            }
            std::cout << (first_r ? "" : "\n    ") << "]}";
        }
        std::cout << (rows.empty() ? "" : "\n  ") << "]\n}\n";
    }
    return 0;
}
