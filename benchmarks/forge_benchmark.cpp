#include "algorithm/AlgorithmRegistry.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "parser/EnchInfoParser.h"
#include "parser/EquipmentParser.h"
#include "parser/TagResolver.h"
#include "registries/EnchantmentRegistry.h"
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
    {"sword_3enchants", "diamond_sword",
     {"sharpness=5", "knockback=2", "unbreaking=3"}, 14, 30},
    {"sword_5enchants", "diamond_sword",
     {"sharpness=5", "knockback=2", "fire_aspect=2", "looting=3", "unbreaking=3"}, 31, 50},
    // 7+ books: DFS/A* search space is large — only run greedy
    {"sword_7enchants", "diamond_sword",
     {"sharpness=5", "sweeping_edge=3", "fire_aspect=2", "knockback=2",
      "looting=3", "unbreaking=3", "mending=1"}, 0, 200},
    {"boots_full", "diamond_boots",
     {"soul_speed=3", "thorns=3", "feather_falling=4", "depth_strider=3",
      "protection=4", "unbreaking=3", "mending=1"}, 0, 200},
};

// ─── Setup ───
void load_builtin_data() {
    auto dir = std::filesystem::path("data") / "builtin";
    TagResolver tags;
    auto ench_infos = EnchInfoParser::parse(dir / "vanilla.json", tags);
    EnchantmentRegistry::get_instance().initialize(ench_infos);
    auto equipments = EquipmentParser::parse(dir / "vanilla.json", tags);
    EquipmentRegistry::get_instance().initialize(equipments);
    platform::Config::get_instance().set_active(platform::MCE::Java);

}

void run_case(const TestCase& tc) {
    const EquipmentType* eq = EquipmentRegistry::get_instance().get(tc.item_type);
    if (!eq) {
        std::cout << "  SKIP: unknown equipment '" << tc.item_type << "'" << std::endl;
        return;
    }

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
    input.target_item = ItemStack(eq, wanted_set, 0, eq->max_durability);
    input.available_items = books;

    bool is_large = tc.wanted.size() >= 7;
    for (const auto& algo_name : {"greedy", "dfs", "astar"}) {
        if (!AlgorithmRegistry::instance().has_algorithm(algo_name)) continue;
        // Skip DFS/A* for large cases (search space explosion)
        if (is_large && std::string(algo_name) != "greedy") {
            if (std::string(algo_name) == "dfs")
                std::cout << "  dfs: skipped (large search space)" << std::endl;
            continue;
        }
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
                  << (ok ? " ✅" : " ⚠️  out of range") << std::endl;
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

    for (const auto& tc : CASES) {
        std::cout << tc.name << " (" << tc.wanted.size() << " enchants):" << std::endl;
        run_case(tc);
        std::cout << std::endl;
    }
    std::cout << "=== Done ===" << std::endl;
    return 0;
}
