#include "registries/AlgorithmRegistry.h"
#include "registries/RegistryAccess.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancingAlgorithm.h"
#include "algorithm/strategies/HierarchicalMergeAlgorithm.h"
#include "algorithm/strategies/IDAStarAlgorithm.h"
#include "adapters/CompactAdapter.h"
#include "parsers/EnchInfoParser.h"
#include "parsers/EquipmentParser.h"
#include "adapters/RegistryResolver.h"
#include "utils/TagResolver.h"
#include "registries/CompactedRegistries.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "types/ForgeConfig.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <filesystem>
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
    int min_cost;
    int max_cost;
};

TestCase CASES[] = {
    // Swords
    {"sword_basic", "diamond_sword",
     {"sharpness=5", "looting=3", "unbreaking=3"}, 14, 35},
    {"sword_combat_5", "diamond_sword",
     {"sharpness=5", "looting=3", "fire_aspect=2", "knockback=2", "unbreaking=3"}, 31, 52},
    {"sword_combat_7", "diamond_sword",
     {"sharpness=5", "sweeping_edge=3", "looting=3", "unbreaking=3",
      "fire_aspect=2", "knockback=2", "mending=1"}, 40, 120},
    // Tools
    {"pickaxe_fortune", "diamond_pickaxe",
     {"efficiency=5", "fortune=3", "unbreaking=3", "mending=1"}, 15, 40},
    {"pickaxe_silk", "diamond_pickaxe",
     {"efficiency=5", "silk_touch=1", "unbreaking=3", "mending=1"}, 15, 40},
    // Ranged
    {"bow_power", "bow",
     {"power=5", "infinity=1", "flame=1", "punch=2", "unbreaking=3"}, 15, 48},
    {"crossbow", "crossbow",
     {"quick_charge=3", "piercing=4", "unbreaking=3", "mending=1"}, 12, 35},
    // Armor
    {"helmet", "diamond_helmet",
     {"protection=4", "aqua_affinity=1", "respiration=3", "mending=1", "unbreaking=3"}, 20, 51},
    {"chestplate", "diamond_chestplate",
     {"protection=4", "thorns=3", "unbreaking=3", "mending=1"}, 20, 55},
    {"leggings", "diamond_leggings",
     {"protection=4", "swift_sneak=3", "unbreaking=3", "mending=1"}, 20, 50},
    {"boots", "diamond_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "unbreaking=3", "mending=1"}, 25, 60},
    {"boots_full", "diamond_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "soul_speed=3",
      "thorns=3", "unbreaking=3", "mending=1"}, 55, 125},
    // Netherite
    {"netherite_sword", "netherite_sword",
     {"sharpness=5", "sweeping_edge=3", "looting=3", "unbreaking=3",
      "fire_aspect=2", "knockback=2", "mending=1", "vanishing_curse=1"}, 45, 160},
    {"netherite_boots", "netherite_boots",
     {"protection=4", "feather_falling=4", "depth_strider=3", "soul_speed=3",
      "thorns=3", "unbreaking=3", "mending=1",
      "vanishing_curse=1", "binding_curse=1"}, 60, 200},
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
    const char* all_algos[] = {"greedy", "dfs", "astar", "penalty_balance", "hierarchical", "idastar"};
    for (auto* a : all_algos) cfg.algos.insert(a);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            cfg.list_only = true;
        } else if (arg == "--test" && i + 1 < argc) {
            cfg.test_names.clear();
            std::istringstream ss(argv[++i]);
            for (std::string tok; std::getline(ss, tok, ','); )
                cfg.test_names.insert(tok);
        } else if (arg == "--group" && i + 1 < argc) {
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
                    std::cerr << "Warning: unknown group '" << tok << "'\n";
            }
        } else if (arg == "--algo" && i + 1 < argc) {
            cfg.algos.clear();
            std::istringstream ss(argv[++i]);
            for (std::string tok; std::getline(ss, tok, ','); )
                cfg.algos.insert(tok);
        } else if (arg == "--help") {
            std::cout << "Usage: forge_benchmark [options]\n"
                      << "  --list                List test cases & groups\n"
                      << "  --test  <names>       Comma-separated test names\n"
                      << "  --group <names>       Comma-separated group names\n"
                      << "  --algo  <names>       Comma-separated algorithm names\n"
                      << "  --help                This help\n"
                      << "\nExamples:\n"
                      << "  forge_benchmark --group netherite\n"
                      << "  forge_benchmark --test netherite_sword,boots_full --algo greedy,dfs\n"
                      << "  forge_benchmark --group armor --algo astar\n";
            std::exit(0);
        }
        else if (arg == "--no-skip") {
            cfg.no_skip = true;
        }
    }
    return cfg;
}

// ─── Setup ───
void load_builtin_data(const std::filesystem::path &builtin_data_dir) {
    TagResolver tags;
    registries::categories().initialize();
    auto &cat_reg = registries::categories();
    auto raw_ench = EnchInfoParser::parse(builtin_data_dir / "vanilla.json", tags);
    auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
    registries::enchants().initialize(ench_infos);
    auto raw_eq = EquipmentParser::parse(builtin_data_dir / "vanilla.json", tags);
    auto equipments = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
    registries::equipment().initialize(equipments);
}

void run_case(const TestCase& tc, const std::unordered_set<std::string>& enabled_algos, bool no_skip = false) {
    int32_t eq_id = registries::equipment().get_id(tc.item_type);
    if (eq_id < 0) {
        std::cout << "  SKIP: unknown equipment '" << tc.item_type << "'" << std::endl;
        return;
    }
    const Equipment& eq = registries::equipment().get(eq_id);

    // ── Build wanted set and graduated books (Issue 5) ──
    ::EnchSet wanted_set;
    ItemCollection books;
    for (const auto& spec : tc.wanted) {
        auto p = spec.find('=');
        std::string id = spec.substr(0, p);
        int32_t lv = std::stoi(spec.substr(p + 1));
        int32_t eid = registries::enchants().get_id(id);
        if (eid < 0) { std::cout << "  SKIP: unknown enchant '" << id << "'" << std::endl; return; }
        wanted_set.emplace(eid, lv);
        // Create graduated books from level 1 up to max level,
        // matching InputParser::generate_books() behavior.
        for (int32_t lvl = 1; lvl <= lv; ++lvl)
            books.emplace_back(::EnchSet{Ench(eid, lvl)});
    }

    compact::EnchReg ench_reg;
    ench_reg.init(registries::enchants(), eq);

    ItemStack start_item(eq, ::EnchSet{}, 0, eq.max_durability);

    AlgorithmInput algo_input;
    algo_input.config.platform = MCE::Java;
    algo_input.equipment = eq;
    algo_input.ench_reg = std::move(ench_reg);

    algo_input.items.push_back(CompactAdapter::from_domain(start_item, algo_input.ench_reg));
    for (const auto& book : books)
        algo_input.items.push_back(CompactAdapter::from_domain(book, algo_input.ench_reg));

    algo_input.target.reserve(wanted_set.size());
    for (const auto& e : wanted_set)
        algo_input.target.push_back({static_cast<int16_t>(e.id), static_cast<int16_t>(e.level)});

    // ── Warmup: one greedy run before measurements (Issue 3) ──
    {
        if (registries::algorithms().contains("greedy")) {
            auto warmup_algo = registries::algorithms().create("greedy");
            AlgorithmExecutor warmup_exec(std::move(warmup_algo));
            AlgorithmInput warmup_input = algo_input;
            warmup_exec.start(std::move(warmup_input));
            warmup_exec.wait();
            // Result discarded
        }
    }

    constexpr int NUM_RUNS = 3;

    for (const auto& algo_name : enabled_algos) {
        if (!registries::algorithms().contains(algo_name)) {
            std::cout << "  " << algo_name << ": unknown, skipping" << std::endl;
            continue;
        }
        if (!no_skip && (algo_name == "astar" || algo_name == "idastar") && tc.wanted.size() > 8) {
            std::cout << "  " << algo_name << ": SKIP: too many enchants" << std::endl;
            continue;
        }

        // ── Aggregate results across 3 runs ──
        int32_t cost_min = std::numeric_limits<int32_t>::max();
        int32_t cost_max = std::numeric_limits<int32_t>::min();
        double cost_sum = 0;
        double self_min = std::numeric_limits<double>::max();
        double self_max = std::numeric_limits<double>::min();
        double self_sum = 0;
        double wall_min = std::numeric_limits<double>::max();
        double wall_max = std::numeric_limits<double>::min();
        double wall_sum = 0;
        int success_count = 0;

        for (int run = 0; run < NUM_RUNS; ++run) {
            auto algo = registries::algorithms().create(algo_name);
            AlgorithmExecutor executor(std::move(algo));

            AlgorithmInput run_input = algo_input;

            auto wall_start = std::chrono::high_resolution_clock::now();
            executor.start(std::move(run_input));
            executor.wait();
            auto wall_end = std::chrono::high_resolution_clock::now();
            double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

            if (executor.state() != AlgorithmState::Completed) {
                continue;
            }
            AlgorithmOutput out = executor.output();
            if (out.steps.empty()) {
                continue;
            }

            int32_t total = 0;
            for (const auto& step_list : out.steps)
                for (const auto& s : step_list)
                    total += s.cost;

            double self_ms = static_cast<double>(out.computation_time.count());

            cost_min = std::min(cost_min, total);
            cost_max = std::max(cost_max, total);
            cost_sum += total;
            self_min = std::min(self_min, self_ms);
            self_max = std::max(self_max, self_ms);
            self_sum += self_ms;
            wall_min = std::min(wall_min, wall_ms);
            wall_max = std::max(wall_max, wall_ms);
            wall_sum += wall_ms;
            ++success_count;
        }

        if (success_count == 0) {
            std::cout << "  " << algo_name << ": FAILED (" << NUM_RUNS << "/" << NUM_RUNS << " runs failed)" << std::endl;
            continue;
        }

        double cost_avg = cost_sum / success_count;
        double self_avg = self_sum / success_count;
        double wall_avg = wall_sum / success_count;

        // All successful runs must be within range
        bool all_ok = cost_min >= tc.min_cost && cost_max <= tc.max_cost;

        std::cout << "  " << algo_name << ": cost(avg=" << cost_avg
                  << " min=" << cost_min << " max=" << cost_max << ")"
                  << " self(avg=" << self_avg << "ms"
                  << " min=" << self_min << "ms"
                  << " max=" << self_max << "ms)"
                  << " wall(avg=" << wall_avg << "ms)"
                  << " [" << tc.min_cost << "-" << tc.max_cost << "L]"
                  << (all_ok ? " ✅" : " ⚠️  out of range")
                  << " runs=" << success_count
                  << std::endl;
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

    // Resolve data path: prefer path relative to executable, fall back to CWD
    auto builtin_data_dir = std::filesystem::absolute(argv[0]).parent_path() / "data" / "builtin";
    if (!std::filesystem::exists(builtin_data_dir)) {
        builtin_data_dir = std::filesystem::path("data") / "builtin";
    }
    load_builtin_data(builtin_data_dir);

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
        std::cout << tc->name << " (" << tc->wanted.size() << " enchants):" << std::endl;
        run_case(*tc, cfg.algos, cfg.no_skip);
        std::cout << std::endl;
    }
    std::cout << "=== Done ===" << std::endl;
    return 0;
}
