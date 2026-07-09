#include "algorithm/AlgorithmRegistry.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancing.h"
#include "algorithm/strategies/HierarchicalMergeStrategy.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/TagResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/PlatformConfig.h"

#include <filesystem>
#include <iostream>
#include <string>
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
};

// ─── Setup ───
void load_builtin_data() {
    auto dir = std::filesystem::path("data") / "builtin";
    TagResolver tags;
    EquipmentCategoryRegistry::get_instance().initialize();
    auto ench_infos = EnchInfoParser::parse(dir / "vanilla.json", tags);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    auto equipments = EquipmentParser::parse(dir / "vanilla.json", tags);
    EquipmentRegistry::get_instance().initialize(equipments);
    platform::Config::get_instance().set_active(platform::MCE::Java);

}

void run_case(const TestCase& tc) {
    int32_t eq_id = EquipmentRegistry::get_instance().get_id(tc.item_type);
    if (eq_id < 0) {
        std::cout << "  SKIP: unknown equipment '" << tc.item_type << "'" << std::endl;
        return;
    }
    const Equipment& eq = EquipmentRegistry::get_instance().get(eq_id);

    // Parse wanted enchantments
    EnchSet wanted_set;
    ItemCollection books;
    for (const auto& spec : tc.wanted) {
        auto p = spec.find('=');
        std::string id = spec.substr(0, p);
        int32_t lv = std::stoi(spec.substr(p + 1));
        int32_t eid = EnchantmentRegistry::get_instance().get_id(id);
        if (eid < 0) { std::cout << "  SKIP: unknown enchant '" << id << "'" << std::endl; return; }
        wanted_set.emplace(eid, lv);
        books.emplace_back(EnchSet{Ench(eid, lv)});
    }

    AlgorithmInput input;
    input.platform = platform::MCE::Java;
    input.original_ench = EnchSet{};
    // target_item.enchantments = GOAL state for DFS/AStar
    input.target_item = ItemStack(&eq, wanted_set, 0, eq.max_durability);
    input.available_items = books;

    for (const auto& algo_name : {"greedy", "dfs", "astar", "penalty_balance", "hierarchical"}) {
        if (!AlgorithmRegistry::instance().has_algorithm(algo_name)) continue;
        auto algo = AlgorithmRegistry::instance().create(algo_name);
        AlgorithmExecutor executor(std::move(algo));
        executor.start(input);
        executor.wait();

        if (executor.state() != AlgorithmState::Completed) {
            std::cout << "  " << algo_name << ": FAILED" << std::endl;
            continue;
        }
        auto out = executor.output();
        if (out.steps.empty()) {
            std::cout << "  " << algo_name << ": no solution" << std::endl;
            continue;
        }

        int32_t total = 0;
        for (auto& sl : out.steps)
            for (auto& s : sl) total += s.exp_level_cost;

        bool ok = total >= tc.min_cost && total <= tc.max_cost;
        std::cout << "  " << algo_name << ": " << total << "L ["
                  << tc.min_cost << "-" << tc.max_cost << "L]"
                  << (ok ? " ✅" : " ⚠️  out of range")
                  << " (" << out.computation_time.count() << "ms)" << std::endl;
    }
}

} // namespace

int main() {
    std::cout << "=== Dataset Benchmark ===" << std::endl;
    load_builtin_data();

    AlgorithmRegistry::instance().register_algorithm("greedy",
        []{ return std::make_unique<GreedyAlgorithm>(); });
    AlgorithmRegistry::instance().register_algorithm("dfs",
        []{ return std::make_unique<DFSAlgorithm>(); });
    AlgorithmRegistry::instance().register_algorithm("astar",
        []{ return std::make_unique<AStarAlgorithm>(); });
    AlgorithmRegistry::instance().register_algorithm("penalty_balance",
        []{ return std::make_unique<DynamicPenaltyBalancing>(); });
    AlgorithmRegistry::instance().register_algorithm("hierarchical",
        []{ return std::make_unique<HierarchicalMergeStrategy>(); });

    for (const auto& tc : CASES) {
        std::cout << tc.name << " (" << tc.wanted.size() << " enchants):" << std::endl;
        run_case(tc);
        std::cout << std::endl;
    }
    std::cout << "=== Done ===" << std::endl;
    return 0;
}
