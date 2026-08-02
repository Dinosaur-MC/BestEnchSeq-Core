#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/types/Enchantment.h"
#include "common/io/json.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

// ─── Algorithm enchant limit matrix ───────────────────────────────────
//
// Each entry defines the maximum enchant count an algorithm can handle
// Each algorithm has a row across tier levels 0..6.  For a given --tier N,
// the algorithm runs only when ench_count ≤ row[N].  -1 = unlimited.
// Higher tier = more permissive; lower tier = more restrictive.
//
//              tier→ 0   1   2   3   4   5   6   ...
struct AlgoLimit {
    const char* name;
    int8_t max_ench_by_tier[9];   // -1 = unlimited
};

static constexpr AlgoLimit ALGO_LIMITS[] = {
    // Fastest: handle any enchant count instantly
    {"hamming",          {-1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {"difficulty_first", {-1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {"penalty_balance",  {-1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Medium: near-optimal, moderate speed
    {"dp_merge",         { 8,  10,  12,14, 16, 16, 16, 16, 16}},
    // Exact DP with B&B bound + Pareto + optional per-step cap: faster than
    // dp_merge, handles more enchantments (direct mode).
    {"bb_dp",            {10,  12,  14,16, 18, 20, 24, 24, 28}},
    // Slow: exact search, practical up to 9 enchants
    {"astar",            { 7,  9,  10,  10, 11, 12, 13, 14, 15}},
    {"idastar",          { 7,  9,  9,  9, 10, 10, 12, 12, 14}},
    // Slowest: dfs caps lower by default
    {"dfs",              { 7,  8,  9,  9,  9,  9, 10, 10, 12}},
};

static constexpr int TIER_LEVELS = 9;  // 0..8
static constexpr int DEFAULT_TIER = 2;

/// Look up an algorithm's limit entry (nullptr if unknown).
static const AlgoLimit* find_limit(const std::string& name) {
    for (const auto& limit : ALGO_LIMITS)
        if (name == limit.name) return &limit;
    return nullptr;
}

/// Get the "main" name from a chain "warmup+main".
static std::string main_name(const std::string& algo) {
    auto p = algo.find('+');
    return (p != std::string::npos) ? algo.substr(p + 1) : algo;
}

/// Check whether \p algo should be skipped for test case with
/// \p ench_count at configured \p tier.  Returns false when \p no_skip.
static bool should_skip(const std::string& algo, int ench_count,
                         int tier, bool no_skip) {
    if (no_skip) return false;
    auto* lim = find_limit(main_name(algo));
    if (!lim) return false;
    int8_t limit = lim->max_ench_by_tier[tier];
    return limit >= 0 && ench_count > limit;
}

/// Print the algorithm limit matrix to stdout.
static void print_algo_limits() {
    std::cout << "\nAlgorithm enchant limits (--tier N selects column):\n";
    std::cout << "  " << std::left << std::setw(20) << "Algorithm";
    for (int t = 0; t < TIER_LEVELS; ++t)
        std::cout << std::right << std::setw(4) << t;
    std::cout << "\n";
    std::cout << "  " << std::string(4 * TIER_LEVELS + 20, '-') << "\n";
    for (const auto& limit : ALGO_LIMITS) {
        std::cout << "  " << std::left << std::setw(20) << limit.name;
        for (int t = 0; t < TIER_LEVELS; ++t) {
            if (limit.max_ench_by_tier[t] < 0)
                std::cout << std::right << std::setw(4) << "--";
            else
                std::cout << std::right << std::setw(4) << (int)limit.max_ench_by_tier[t];
        }
        std::cout << "\n";
    }
    std::cout << "  (default tier " << DEFAULT_TIER
              << ".  --no-skip disables all limits.)\n";
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
        } else if (arg == "--help") {
            std::cout << "Usage: forge_benchmark [options]\n"
                      << "  --list                List test cases, groups & algorithm limits\n"
                      << "  --test  <names>       Comma-separated test names\n"
                      << "  --group <names>       Comma-separated group names\n"
                      << "  --alg   <names>       Comma-separated algorithm names (--algo also accepted)\n"
                      << "  --algo-dir <dir>      Load algorithm plugins from directory\n"
                      << "  --tier <num>          Algorithm permissiveness level (0-" << TIER_LEVELS - 1 << ", default " << DEFAULT_TIER << ")\n"
                      << "  --no-skip             Run all algorithms without enchant limits\n"
                      << "  --help                This help\n";
            print_algo_limits();
            std::cout << "\nExamples:\n"
                      << "  forge_benchmark --group netherite\n"
                      << "  forge_benchmark --test netherite_sword,boots_full --alg dp_merge\n"
                      << "  forge_benchmark --group armor --alg astar\n"
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
                  const algorithm::EnchSet& target, int32_t max_cost) {
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

void run_case(const TestCase& tc, const Profile& profile,
              const std::vector<std::string>& algos,
              const algorithm::AlgorithmLoader& loader,
              int tier, bool no_skip) {
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
    algo_input.config.search.max_search_time = std::chrono::seconds(60 * 10);
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
        std::cout << "  " << std::left << std::setw(18) << algo_name;

        auto plus = algo_name.find('+');
        if (plus != std::string::npos) {
            std::string warmup_name = algo_name.substr(0, plus);
            std::string main_name   = algo_name.substr(plus + 1);

            if (should_skip(main_name, static_cast<int>(tc.wanted.size()), tier, no_skip)) {
                std::cout << "SKIP (too many enchants)" << std::endl;
                continue;
            }
            try {
                auto main_algo = loader.create(main_name);
                algorithm::AlgorithmExecutor executor(std::move(main_algo));
                algorithm::AlgorithmInput run_input = algo_input;
                executor.start(std::move(run_input),
                               loader.create(warmup_name));
                executor.wait();
                if (executor.state() != algorithm::AlgorithmState::Completed) {
                    std::cout << "no solution" << std::endl; continue;
                }
                algorithm::AlgorithmOutput out = executor.output();
                if (out.solutions.empty()) {
                    std::cout << "no solution" << std::endl; continue;
                }
                print_result(out, algo_input.target.enchs, tc.max_cost);
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << std::endl;
            }
            continue;
        }

        if (should_skip(algo_name, static_cast<int>(tc.wanted.size()), tier, no_skip)) {
            std::cout << "SKIP (too many enchants)" << std::endl;
            continue;
        }
        try {
            auto algo = loader.create(algo_name);
            algorithm::AlgorithmExecutor executor(std::move(algo));
            algorithm::AlgorithmInput run_input = algo_input;
            executor.start(std::move(run_input));
            executor.wait();

            if (executor.state() != algorithm::AlgorithmState::Completed) {
                std::cout << "no solution" << std::endl; continue;
            }
            algorithm::AlgorithmOutput out = executor.output();
            if (out.solutions.empty()) {
                std::cout << "no solution" << std::endl; continue;
            }
            print_result(out, algo_input.target.enchs, tc.max_cost);
        } catch (const std::exception& e) {
            std::cout << "ERROR: " << e.what() << std::endl;
        }
    }
}

void list_cases() {
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
    print_algo_limits();
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // ── Load profiles & test cases before CLI parsing (needed for group validation) ──
    std::cout << "=== Dataset Benchmark ===\n"
              << "Loading profiles..." << std::endl;
    load_profiles("data/tests/profiles");

    std::cout << "Loading test cases..." << std::endl;
    load_testcases("data/tests/testcases");
    std::cout << std::endl;

    BenchConfig cfg = parse_cli(argc, argv);

    if (cfg.list_only) {
        list_cases();
        return 0;
    }

    std::cout << "Time: "
              << std::chrono::current_zone()->to_local(
                     std::chrono::system_clock::now())
              << std::endl;

    // ═════════════════════════════════════════════════════════════════════
    // Load algorithms: built-in + optional plugins
    // ═════════════════════════════════════════════════════════════════════
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
    for (auto& qc : queue) {
        std::cout << "\n" << qc.tc->name << " (" << qc.tc->wanted.size()
                  << " enchants, max " << qc.tc->max_cost << "L):" << std::endl;
        try {
            run_case(*qc.tc, *qc.profile, algos, loader, cfg.tier, cfg.no_skip);
        } catch (const std::exception& e) {
            std::cerr << "  ERROR: " << e.what() << std::endl;
        }
    }

    std::cout << "\n=== Done ===" << std::endl;
    return 0;
}
