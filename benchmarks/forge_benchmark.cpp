#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "builtin/DataLoader.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Enchantment.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// ─── Inline dataset ───
struct TestCase {
    std::string name;
    std::string item_type;
    std::vector<std::string> wanted;
    int max_cost;   // upper bound reference (Minecraft anvil cap: 39)
};

TestCase CASES[] = {
    // Swords
    {"sword_basic", "diamond_sword",
     {"sharpness=5", "looting=3", "unbreaking=3"}, 30},
    {"sword_combat_5", "diamond_sword",
     {"sharpness=5", "looting=3", "fire_aspect=2", "knockback=2", "unbreaking=3"}, 40},
    {"sword_combat_7", "diamond_sword",
     {"sharpness=5", "sweeping_edge=3", "looting=3", "unbreaking=3",
      "fire_aspect=2", "knockback=2", "mending=1"}, 56},
    // Tools
    {"pickaxe_fortune", "diamond_pickaxe",
     {"efficiency=5", "fortune=3", "unbreaking=3", "mending=1"}, 36},
    {"pickaxe_silk", "diamond_pickaxe",
     {"efficiency=5", "silk_touch=1", "unbreaking=3", "mending=1"}, 36},
    // Ranged
    {"bow_power", "bow",
     {"power=5", "infinity=1", "flame=1", "punch=2", "unbreaking=3"}, 40},
    {"crossbow", "crossbow",
     {"quick_charge=3", "piercing=4", "unbreaking=3", "mending=1"}, 30},
    // Armor
    {"helmet", "diamond_helmet",
     {"protection=4", "aqua_affinity=1", "respiration=3", "mending=1", "unbreaking=3"}, 36},
    {"chestplate", "diamond_chestplate",
     {"protection=4", "thorns=3", "unbreaking=3", "mending=1"}, 40},
    {"leggings", "diamond_leggings",
     {"protection=4", "swift_sneak=3", "unbreaking=3", "mending=1"}, 40},
    {"boots", "diamond_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "unbreaking=3", "mending=1"}, 40},
    {"boots_full", "diamond_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "soul_speed=3",
      "thorns=3", "unbreaking=3", "mending=1"}, 72},
    // Netherite
    {"netherite_sword", "netherite_sword",
     {"sharpness=5", "sweeping_edge=3", "looting=3", "unbreaking=3",
      "fire_aspect=2", "knockback=2", "mending=1", "vanishing_curse=1"}, 100},
    {"netherite_boots", "netherite_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "soul_speed=3",
      "thorns=3", "unbreaking=3", "mending=1",
      "vanishing_curse=1", "binding_curse=1"}, 150},
};

// ─── Groups ───
struct GroupMap { const char* name; std::initializer_list<const char*> members; };
const GroupMap GROUPS[] = {
    {"sword",     {"sword_basic", "sword_combat_5", "sword_combat_7"}},
    {"tool",      {"pickaxe_fortune", "pickaxe_silk"}},
    {"ranged",    {"bow_power", "crossbow"}},
    {"armor",     {"helmet", "chestplate", "leggings", "boots", "boots_full"}},
    {"netherite", {"netherite_sword", "netherite_boots"}},
};

// ─── Helper: comma-separated list → unordered_set ───
static std::unordered_set<std::string> split_csv(const std::string& s) {
    std::unordered_set<std::string> out;
    std::istringstream ss(s);
    for (std::string tok; std::getline(ss, tok, ',');)
        if (!tok.empty()) out.insert(tok);
    return out;
}

// ─── CLI flag parsing ───
//
// Algorithm validation is deferred: --alg values are stored raw and later
// intersected with what the AlgorithmLoader actually has (after --algo-dir
// plugins are loaded and built-in strategies are registered).
struct BenchConfig {
    std::unordered_set<std::string> test_names; // empty = all
    std::unordered_set<std::string> raw_algos;  // from --alg; empty = all loaded
    std::string algo_dir;                       // plugin dir, empty = none
    bool list_only = false;
    bool no_skip = false;
};

BenchConfig parse_cli(int argc, char* argv[]) {
    BenchConfig cfg;

    auto die = [](const std::string& msg) {
        std::cerr << "Error: " << msg << "\n"
                  << "Usage: forge_benchmark [options]\n"
                  << "  --list                List test cases & groups\n"
                  << "  --test  <names>       Comma-separated test names\n"
                  << "  --group <names>       Comma-separated group names\n"
                  << "  --alg   <names>       Comma-separated algorithm names (--algo also accepted)\n"
                  << "  --algo-dir <dir>      Load algorithm plugins from directory\n"
                  << "  --no-skip             Run all algorithms (by default astar/idastar are skipped for >8 enchants)\n"
                  << "  --help                This help\n";
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
                for (const auto& g : GROUPS) {
                    if (tok == g.name) {
                        for (auto* m : g.members)
                            cfg.test_names.insert(m);
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
        } else if (arg == "--no-skip") {
            cfg.no_skip = true;
        } else if (arg == "--help") {
            std::cout << "Usage: forge_benchmark [options]\n"
                      << "  --list                List test cases & groups\n"
                      << "  --test  <names>       Comma-separated test names\n"
                      << "  --group <names>       Comma-separated group names\n"
                      << "  --alg   <names>       Comma-separated algorithm names (--algo also accepted)\n"
                      << "  --algo-dir <dir>      Load algorithm plugins from directory\n"
                      << "  --no-skip             Run all algorithms (by default astar/idastar are skipped for >8 enchants)\n"
                      << "  --help                This help\n"
                      << "\nExamples:\n"
                      << "  forge_benchmark --group netherite\n"
                      << "  forge_benchmark --test netherite_sword,boots_full --alg greedy,dfs\n"
                      << "  forge_benchmark --group armor --alg astar\n"
                      << "  forge_benchmark --algo-dir build/plugins --group sword\n";
            std::exit(0);
        } else if (arg.size() > 1 && arg[0] == '-') {
            die("unknown flag '" + arg + "'");
        }
    }

    // Validate: --test names must exist
    if (!cfg.test_names.empty()) {
        std::unordered_set<std::string> valid_tests;
        for (const auto& tc : CASES)
            valid_tests.insert(tc.name);
        for (const auto& t : cfg.test_names)
            if (!valid_tests.contains(t))
                die("unknown test '" + t + "'");
    }

    return cfg;
}

// ─── Resolve algorithm list after loader is ready ───
//
// Returns sorted unique names (including chain "warmup+main" entries).
// When --alg was given, warns about any requested algorithm that isn't loaded
// and silently excludes it.
static std::vector<std::string>
resolve_algos(const BenchConfig& cfg, const algorithm::AlgorithmLoader& loader) {
    std::vector<std::string> all = loader.list();
    std::sort(all.begin(), all.end());

    if (cfg.raw_algos.empty())
        return all;  // every loaded algorithm

    // Filter to only those the user asked for
    std::vector<std::string> filtered;
    for (const auto& name : cfg.raw_algos) {
        auto plus = name.find('+');
        if (plus != std::string::npos) {
            // Chain syntax: warmup+main
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

// ─── Global registries (initialised once at startup) ──
static EnchantmentRegistry G_ENCH;
static EquipmentRegistry G_EQ;
static EquipmentTagRegistry G_CAT;

// ─── Setup ───
void load_builtin_data() {
    besq::data::load_builtin_data(G_CAT, G_ENCH, G_EQ);
}

// ─── Run a single test case against every <algos> entry ───
void run_case(const TestCase& tc, const std::vector<std::string>& algos,
              const algorithm::AlgorithmLoader& loader, bool no_skip) {
    auto eq_it = G_EQ.find(NSID(tc.item_type));
    if (eq_it == G_EQ.end()) {
        std::cout << "  [SKIP] unknown equipment '" << tc.item_type << "'\n";
        return;
    }
    const Equipment& eq = *eq_it;

    // ── Parse wanted enchants + build business items ───────────────────
    ::EnchSet wanted_set;
    ItemCollection books;
    for (const auto& spec : tc.wanted) {
        auto p = spec.find('=');
        std::string id = spec.substr(0, p);
        int32_t lv = std::stoi(spec.substr(p + 1));
        auto ench_it = G_ENCH.find(NSID(id));
        if (ench_it == G_ENCH.end()) {
            std::cout << "  [SKIP] unknown enchant '" << id << "'\n";
            return;
        }
        Ench ench{ench_it->id, ench_it->name, lv};
        wanted_set.insert(ench);
        books.emplace_back(NSID("minecraft:enchanted_book"),
                           ::EnchSet{std::move(ench)}, 0);
    }

    // ── Build compact EnchReg for this equipment ───────────────────────
    algorithm::EnchReg ench_reg;
    {
        // Build sorted applicable EnchInfos + NSID mapping
        const auto& all_infos = G_ENCH.data();
        std::vector<std::pair<NSID, EnchInfo>> sorted(all_infos.begin(), all_infos.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<algorithm::EnchInfo> algo_infos;
        std::vector<NSID> global_ids;
        std::unordered_map<NSID, int16_t> nsid_to_local;

        for (int32_t gid = 0; gid < static_cast<int32_t>(sorted.size()); ++gid) {
            const auto& biz = sorted[gid].second;
            bool applicable = biz.applicable_equipments.count(eq.category) > 0
                           || biz.applicable_equipments.count(NSID("#minecraft:any")) > 0;
            if (!applicable) continue;

            algorithm::EnchInfo ai;
            ai.mul     = static_cast<uint16_t>(biz.multiplier);
            ai.mul_b   = static_cast<uint16_t>(std::max(1, biz.multiplier >> 1));
            ai.max_lvl = static_cast<uint16_t>(biz.max_level);
            ai.exc_mask.resize(algo_infos.size() / algorithm::MASK_ELEM_SIZE + 1, 0);
            for (size_t li = 0; li < algo_infos.size(); ++li) {
                if (biz.exclusive_set.count(global_ids[li])) {
                    size_t word = li / algorithm::MASK_ELEM_SIZE;
                    size_t bit  = li % algorithm::MASK_ELEM_SIZE;
                    ai.exc_mask[word] |= (algorithm::MaskType(1) << bit);
                    if (word < algo_infos[li].exc_mask.size())
                        algo_infos[li].exc_mask[word] |= (algorithm::MaskType(1) << bit);
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
    algo_input.f_config.platform = MCE::Java;
    algo_input.ench_reg = std::move(ench_reg);

    // Convert target enchantments
    {
        algorithm::EnchSet target_enchs;
        for (const auto& e : wanted_set) {
            int16_t lid = algo_input.ench_reg.to_local_id(e.id);
            if (lid >= 0)
                target_enchs.insert(algorithm::Ench{lid, static_cast<int16_t>(e.level)});
        }
        algo_input.target.enchs = std::move(target_enchs);
        algo_input.target.type  = algorithm::ItemType::Equip;
        algo_input.target.ppn   = 0;
        algo_input.target.dur   = static_cast<int16_t>(eq.max_durability);
    }
    algo_input.mode = AlgorithmMode::direct;
    algo_input.data = algorithm::EnchCollection{};

    // Convert business items -> algorithm items
    Item start_item(eq.id, ::EnchSet{}, 0, eq.max_durability);
    algo_input.items.push_back(
        CompactAdapter::from_domain(start_item, algo_input.ench_reg));
    for (const auto& book : books)
        algo_input.items.push_back(
            CompactAdapter::from_domain(book, algo_input.ench_reg));

    // ══════════════════════════════════════════════════════════════════════
    // Iterate algos in sorted order
    // ══════════════════════════════════════════════════════════════════════
    for (const auto& algo_name : algos) {
        std::cout << "  " << std::left << std::setw(18) << algo_name;

        // ── Chain: warmup+main ──────────────────────────────────────────
        auto plus = algo_name.find('+');
        if (plus != std::string::npos) {
            std::string warmup_name = algo_name.substr(0, plus);
            std::string main_name   = algo_name.substr(plus + 1);

            if (!no_skip && (main_name == "astar" || main_name == "idastar" || main_name == "dfs")
                && tc.wanted.size() > 9) {
                std::cout << "SKIP (too many enchants)\n";
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
                    std::cout << "no solution\n"; continue;
                }
                algorithm::AlgorithmOutput out = executor.output();
                if (out.solutions.empty()) {
                    std::cout << "no solution\n"; continue;
                }
                int32_t total = out.solutions[0].total_cost;
                bool ok = total <= tc.max_cost;
                std::cout << std::right << std::setw(4) << total << "L"
                          << (ok ? "  \xe2\x9c\x85" : "  \xe2\x9a\xa0")
                          << "  " << std::setw(4) << out.computation_time.count()
                          << "ms\n";
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << '\n';
            }
            continue;
        }

        // ── Single algorithm ────────────────────────────────────────────
        if (!no_skip && (algo_name == "astar" || algo_name == "idastar" || algo_name == "dfs")
            && tc.wanted.size() > 9) {
            std::cout << "SKIP (too many enchants)\n";
            continue;
        }

        try {
            auto algo = loader.create(algo_name);
            algorithm::AlgorithmExecutor executor(std::move(algo));
            algorithm::AlgorithmInput run_input = algo_input;
            executor.start(std::move(run_input));
            executor.wait();

            if (executor.state() != algorithm::AlgorithmState::Completed) {
                std::cout << "no solution\n"; continue;
            }
            algorithm::AlgorithmOutput out = executor.output();
            if (out.solutions.empty()) {
                std::cout << "no solution\n"; continue;
            }
            int32_t total = out.solutions[0].total_cost;
            bool ok = total <= tc.max_cost;
            std::cout << std::right << std::setw(4) << total << "L"
                      << (ok ? "  \xe2\x9c\x85" : "  \xe2\x9a\xa0")
                      << "  " << std::setw(4) << out.computation_time.count()
                      << "ms\n";
        } catch (const std::exception& e) {
            std::cout << "ERROR: " << e.what() << '\n';
        }
    }
}

void list_cases() {
    std::cout << "Test cases (" << (sizeof(CASES) / sizeof(CASES[0])) << " total):\n";
    for (const auto& tc : CASES)
        std::cout << "  " << tc.name << "  (" << tc.item_type << ", "
                  << tc.wanted.size() << " enchants)\n";
    std::cout << "\nGroups:\n";
    for (const auto& g : GROUPS) {
        std::cout << "  " << g.name << ":";
        for (auto* m : g.members) std::cout << " " << m;
        std::cout << "\n";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    BenchConfig cfg = parse_cli(argc, argv);

    if (cfg.list_only) {
        list_cases();
        return 0;
    }

    std::cout << "Time: "
              << std::chrono::current_zone()->to_local(
                     std::chrono::system_clock::now())
              << std::endl;
    std::cout << "=== Dataset Benchmark ===" << std::endl;

    load_builtin_data();

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

    // Resolve effective algorithm list
    std::vector<std::string> algos = resolve_algos(cfg, loader);
    if (algos.empty()) {
        std::cerr << "No algorithms available after filtering. "
                  << "Loaded " << loader.size() << " total." << std::endl;
        return 1;
    }

    // Show context
    std::cout << "Using " << algos.size() << " algorithm(s)"
              << " (" << loader.size() << " loaded)"
              << (cfg.algo_dir.empty() ? ". Use --algo-dir to load plugins."
                                       : ".")
              << std::endl;

    // Build test queue
    std::vector<const TestCase*> queue;
    if (cfg.test_names.empty()) {
        for (const auto& tc : CASES) queue.push_back(&tc);
    } else {
        for (const auto& tc : CASES)
            if (cfg.test_names.count(tc.name))
                queue.push_back(&tc);
    }

    if (queue.empty()) {
        std::cerr << "No matching test cases. Use --list to see available tests."
                  << std::endl;
        return 1;
    }

    // Run each test case
    for (auto* tc : queue) {
        std::cout << "\n" << tc->name << " (" << tc->wanted.size()
                  << " enchants, max " << tc->max_cost << "L):" << std::endl;
        try {
            run_case(*tc, algos, loader, cfg.no_skip);
        } catch (const std::exception& e) {
            std::cerr << "  ERROR: " << e.what() << std::endl;
        }
    }

    std::cout << "\n=== Done ===" << std::endl;
    return 0;
}
