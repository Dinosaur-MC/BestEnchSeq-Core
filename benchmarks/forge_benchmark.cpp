#include "registries/AlgorithmRegistry.h"
#include "registries/RegistryAccess.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/Strategies.h"
#include "adapters/CompactAdapter.h"
#include "data/DataLoader.h"
#include "registries/TagResolver.hpp"
#include "registries/CompactedRegistries.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "config/ForgeConfig.h"

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
     {"sharpness=5", "looting=3", "unbreaking=3"}, 35},
    {"sword_combat_5", "diamond_sword",
     {"sharpness=5", "looting=3", "fire_aspect=2", "knockback=2", "unbreaking=3"}, 52},
    {"sword_combat_7", "diamond_sword",
     {"sharpness=5", "sweeping_edge=3", "looting=3", "unbreaking=3",
      "fire_aspect=2", "knockback=2", "mending=1"}, 120},
    // Tools
    {"pickaxe_fortune", "diamond_pickaxe",
     {"efficiency=5", "fortune=3", "unbreaking=3", "mending=1"}, 40},
    {"pickaxe_silk", "diamond_pickaxe",
     {"efficiency=5", "silk_touch=1", "unbreaking=3", "mending=1"}, 40},
    // Ranged
    {"bow_power", "bow",
     {"power=5", "infinity=1", "flame=1", "punch=2", "unbreaking=3"}, 48},
    {"crossbow", "crossbow",
     {"quick_charge=3", "piercing=4", "unbreaking=3", "mending=1"}, 35},
    // Armor
    {"helmet", "diamond_helmet",
     {"protection=4", "aqua_affinity=1", "respiration=3", "mending=1", "unbreaking=3"}, 51},
    {"chestplate", "diamond_chestplate",
     {"protection=4", "thorns=3", "unbreaking=3", "mending=1"}, 55},
    {"leggings", "diamond_leggings",
     {"protection=4", "swift_sneak=3", "unbreaking=3", "mending=1"}, 50},
    {"boots", "diamond_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "unbreaking=3", "mending=1"}, 60},
    {"boots_full", "diamond_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "soul_speed=3",
      "thorns=3", "unbreaking=3", "mending=1"}, 125},
    // Netherite
    {"netherite_sword", "netherite_sword",
     {"sharpness=5", "sweeping_edge=3", "looting=3", "unbreaking=3",
      "fire_aspect=2", "knockback=2", "mending=1", "vanishing_curse=1"}, 160},
    {"netherite_boots", "netherite_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "soul_speed=3",
      "thorns=3", "unbreaking=3", "mending=1",
      "vanishing_curse=1", "binding_curse=1"}, 200},
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

// ─── CLI flag parsing ───
struct BenchConfig {
    std::unordered_set<std::string> test_names; // empty = all
    std::unordered_set<std::string> algos;      // empty = all
    bool list_only = false;
    bool no_skip = false;
};

BenchConfig parse_cli(int argc, char* argv[]) {
    BenchConfig cfg;
    const char* all_algos[] = {"greedy", "dfs", "astar", "penalty_balance", "hierarchical", "idastar", "hamming", "difficulty_first"};
    for (auto* a : all_algos) cfg.algos.insert(a);

    auto die = [](const std::string& msg) {
        std::cerr << "Error: " << msg << "\n"
                  << "Usage: forge_benchmark --alg <names> | --help\n";
        std::exit(1);
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            cfg.list_only = true;
        } else if (arg == "--test") {
            if (i + 1 >= argc) die("--test requires a value");
            cfg.test_names.clear();
            std::istringstream ss(argv[++i]);
            for (std::string tok; std::getline(ss, tok, ','); )
                if (!tok.empty()) cfg.test_names.insert(tok);
        } else if (arg == "--group") {
            if (i + 1 >= argc) die("--group requires a value");
            cfg.test_names.clear();
            std::istringstream ss(argv[++i]);
            for (std::string tok; std::getline(ss, tok, ','); ) {
                bool found = false;
                for (const auto& g : GROUPS) {
                    if (tok == g.name) {
                        for (auto* m : g.members) cfg.test_names.insert(m);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    die("unknown group '" + tok + "'");
            }
        } else if (arg == "--algo" || arg == "--alg") {
            if (i + 1 >= argc) die("--alg requires a value");
            cfg.algos.clear();
            std::istringstream ss(argv[++i]);
            for (std::string tok; std::getline(ss, tok, ','); ) {
                if (!tok.empty())
                    cfg.algos.insert(tok);
            }
        } else if (arg == "--help") {
            std::cout << "Usage: forge_benchmark [options]\n"
                      << "  --list                List test cases & groups\n"
                      << "  --test  <names>       Comma-separated test names\n"
                      << "  --group <names>       Comma-separated group names\n"
                      << "  --alg   <names>       Comma-separated algorithm names (--algo also accepted)\n"
                      << "  --help                This help\n"
                      << "  --no-skip             Always run all algorithms (skip AStar/IDAStar for >8 enchants)\n"
                      << "\nExamples:\n"
                      << "  forge_benchmark --group netherite\n"
                      << "  forge_benchmark --test netherite_sword,boots_full --alg greedy,dfs\n"
                      << "  forge_benchmark --group armor --alg astar\n"
                      << "  forge_benchmark --alg astar,hamming+astar --group netherite --no-skip\n";
            std::exit(0);
        }
        else if (arg == "--no-skip") {
            cfg.no_skip = true;
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

    // Validate: --alg values must be known (or chain with '+')
    std::vector<const char*> all_valid;
    for (auto* a : all_algos) all_valid.push_back(a);
    for (const auto& a : cfg.algos) {
        auto plus = a.find('+');
        if (plus != std::string::npos) {
            std::string w = a.substr(0, plus);
            std::string m = a.substr(plus + 1);
            bool ok_w = std::find(all_valid.begin(), all_valid.end(), w) != all_valid.end();
            bool ok_m = std::find(all_valid.begin(), all_valid.end(), m) != all_valid.end();
            if (!ok_w) die("unknown warmup algorithm '" + w + "' in chain '" + a + "'");
            if (!ok_m) die("unknown main algorithm '" + m + "' in chain '" + a + "'");
        } else {
            if (std::find(all_valid.begin(), all_valid.end(), a) == all_valid.end())
                die("unknown algorithm '" + a + "'");
        }
    }
    return cfg;
}

// ─── Setup ───
void load_builtin_data() {
    TagResolver tags;
    registries::categories().initialize();
    besq::data::load_builtin_data(tags, registries::categories(),
                                  registries::enchants(), registries::equipment());
}

void run_case(const TestCase& tc, const std::unordered_set<std::string>& enabled_algos, bool no_skip = false) {
    int32_t eq_id = registries::equipment().get_id(tc.item_type);
    if (eq_id < 0) {
        std::cout << "  SKIP: unknown equipment '" << tc.item_type << "'" << std::endl;
        return;
    }
    const Equipment& eq = registries::equipment().get(eq_id);

    ::EnchSet wanted_set;
    ItemCollection books;
    for (const auto& spec : tc.wanted) {
        auto p = spec.find('=');
        std::string id = spec.substr(0, p);
        int32_t lv = std::stoi(spec.substr(p + 1));
        int32_t eid = registries::enchants().get_id(id);
        if (eid < 0) { std::cout << "  SKIP: unknown enchant '" << id << "'" << std::endl; return; }
        wanted_set.emplace(eid, lv);
        books.emplace_back(::EnchSet{Ench(eid, lv)});
    }

    compact::EnchReg ench_reg;
    ench_reg.init(registries::enchants(), eq);

    ItemStack start_item(eq, ::EnchSet{}, 0, eq.max_durability);

    AlgorithmInput algo_input;
    algo_input.config.platform = MCE::Java;
    algo_input.ench_reg = std::move(ench_reg);

    algo_input.items.push_back(CompactAdapter::from_domain(start_item, algo_input.ench_reg));
    for (const auto& book : books)
        algo_input.items.push_back(CompactAdapter::from_domain(book, algo_input.ench_reg));

    algo_input.target.reserve(wanted_set.size());
    for (const auto& e : wanted_set) {
        int16_t _lid = static_cast<int16_t>(algo_input.ench_reg.to_local_id(e.id));
        if (_lid >= 0)
            algo_input.target.push_back({_lid, static_cast<int16_t>(e.level)});
    }

    struct BenchResult {
        std::string algo;
        enum Kind { Data, Skip, Fail } kind;
        int32_t cost{0};
        int64_t ms{0};
        bool ok{false};
    };
    std::vector<BenchResult> results;

    for (const auto& algo_name : enabled_algos) {
        // Chain: warmup+main (e.g. "hamming+astar")
        auto plus = algo_name.find('+');
        if (plus != std::string::npos) {
            std::string warmup_name = algo_name.substr(0, plus);
            std::string main_name = algo_name.substr(plus + 1);
            if (!registries::algorithms().contains(warmup_name)
             || !registries::algorithms().contains(main_name)) {
                results.push_back({algo_name, BenchResult::Fail});
                continue;
            }
            if (!no_skip && (main_name == "astar" || main_name == "idastar") && tc.wanted.size() > 8) {
                results.push_back({algo_name, BenchResult::Skip});
                continue;
            }
            auto main_algo = registries::algorithms().create(main_name);
            AlgorithmExecutor executor(std::move(main_algo));
            AlgorithmInput run_input = algo_input;
            executor.start(std::move(run_input),
                          registries::algorithms().create(warmup_name));
            executor.wait();
            if (executor.state() != AlgorithmState::Completed) {
                results.push_back({algo_name, BenchResult::Fail});
                continue;
            }
            AlgorithmOutput out = executor.output();
            if (out.solutions.empty()) {
                results.push_back({algo_name, BenchResult::Fail});
                continue;
            }
            int32_t total = out.solutions[0].total_cost;
            results.push_back({algo_name, BenchResult::Data, total,
                               out.computation_time.count(),
                               total <= tc.max_cost});
            continue;
        }

        if (!registries::algorithms().contains(algo_name)) {
            results.push_back({algo_name, BenchResult::Fail});
            continue;
        }
        if (!no_skip && (algo_name == "astar" || algo_name == "idastar") && tc.wanted.size() > 8) {
            results.push_back({algo_name, BenchResult::Skip});
            continue;
        }

        auto algo = registries::algorithms().create(algo_name);
        AlgorithmExecutor executor(std::move(algo));

        AlgorithmInput run_input = algo_input;
        executor.start(std::move(run_input));
        executor.wait();

        if (executor.state() != AlgorithmState::Completed) {
            results.push_back({algo_name, BenchResult::Fail});
            continue;
        }
        AlgorithmOutput out = executor.output();
        if (out.solutions.empty()) {
            results.push_back({algo_name, BenchResult::Fail});
            continue;
        }

        int32_t total = out.solutions[0].total_cost;

        results.push_back({algo_name, BenchResult::Data, total,
                           out.computation_time.count(),
                           total <= tc.max_cost});
    }

    // Sort by algorithm name (SKIP/FAIL interleaved correctly)
    std::sort(results.begin(), results.end(),
              [](const BenchResult& a, const BenchResult& b) {
                  return a.algo < b.algo;
              });

    for (const auto& r : results) {
        std::cout << "  " << std::left << std::setw(18) << r.algo;
        if (r.kind == BenchResult::Skip) {
            std::cout << "SKIP (too many enchants)";
        } else if (r.kind == BenchResult::Fail) {
            std::cout << "no solution";
        } else {
            std::cout << std::right << std::setw(4) << r.cost << "L"
                      << (r.ok ? "  ✅" : "  ⚠")
                      << "  " << std::setw(4) << r.ms << "ms";
        }
        std::cout << std::endl;
    }
}

void list_cases() {
    std::cout << "Test cases (" << (sizeof(CASES)/sizeof(CASES[0])) << " total):\n";
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

    std::cout << "Time: " << std::chrono::current_zone()->to_local(std::chrono::system_clock::now()) << std::endl;
    std::cout << "=== Dataset Benchmark ===" << std::endl;
    load_builtin_data();

    registries::algorithms().register_algorithm("greedy",
        []{ return std::make_unique<GreedyAlgorithm>(); });
    registries::algorithms().register_algorithm("dfs",
        []{ return std::make_unique<DFSAlgorithm>(); });
    registries::algorithms().register_algorithm("astar",
        []{ return std::make_unique<AStarAlgorithm>(); });
    registries::algorithms().register_algorithm("penalty_balance",
        []{ return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
    registries::algorithms().register_algorithm("hierarchical",
        []{ return std::make_unique<HierarchicalMergeAlgorithm>(); });
    registries::algorithms().register_algorithm("idastar",
        []{ return std::make_unique<IDAStarAlgorithm>(); });
    registries::algorithms().register_algorithm("hamming",
        []{ return std::make_unique<HammingAlgorithm>(); });
    registries::algorithms().register_algorithm("difficulty_first",
        []{ return std::make_unique<DiffFirstAlgorithm>(); });

    // Filter tests
    std::vector<const TestCase*> queue;
    if (cfg.test_names.empty()) {
        for (const auto& tc : CASES) queue.push_back(&tc);
    } else {
        for (const auto& tc : CASES)
            if (cfg.test_names.count(tc.name))
                queue.push_back(&tc);
    }

    if (queue.empty()) {
        std::cerr << "No matching test cases. Use --list to see available tests." << std::endl;
        return 1;
    }

    for (auto* tc : queue) {
        std::cout << "\n" << tc->name << " (" << tc->wanted.size() << " enchants, max "
                  << tc->max_cost << "L):" << std::endl;
        try {
            run_case(*tc, cfg.algos, cfg.no_skip);
        } catch (const std::exception& e) {
            std::cerr << "  ERROR: " << e.what() << std::endl;
        }
    }
    std::cout << "=== Done ===" << std::endl;
    return 0;
}
