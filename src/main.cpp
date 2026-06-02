#include "algorithm/BaseAlgorithm.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string platform_name(platform::MCE platform) {
    switch (platform) {
    case platform::MCE::Java:
        return "Java";
    case platform::MCE::Bedrock:
        return "Bedrock";
    case platform::MCE::All:
        return "All";
    default:
        return "None";
    }
}

std::string to_roman(int32_t level) {
    switch (level) {
    case 1:
        return "I";
    case 2:
        return "II";
    case 3:
        return "III";
    case 4:
        return "IV";
    case 5:
        return "V";
    default:
        return std::to_string(level);
    }
}

std::vector<std::string> describe_enchantments(const EnchSet &enchantments) {
    std::vector<std::string> parts;
    parts.reserve(enchantments.size());
    for (const Ench &enchantment : enchantments) {
        parts.push_back(enchantment.get_name() + " " + to_roman(enchantment.level));
    }
    std::sort(parts.begin(), parts.end());
    return parts;
}

std::string join(const std::vector<std::string> &parts, const std::string &separator) {
    if (parts.empty())
        return "none";

    std::string result = parts.front();
    for (size_t i = 1; i < parts.size(); ++i) {
        result += separator + parts[i];
    }
    return result;
}

std::string describe_item(const ItemStack &item) {
    const std::string enchantment_text = join(describe_enchantments(item.enchantments), ", ");
    if (item.is_book()) {
        return "book[" + enchantment_text + "]";
    }
    if (item.is_equipment()) {
        return item.equipment->name + "[" + enchantment_text + "]";
    }
    return "item[" + enchantment_text + "]";
}

bool matches_target(const ItemStack &actual, const ItemStack &target) {
    return actual.is_equipment() == target.is_equipment() && actual.equipment == target.equipment &&
           actual.enchantments == target.enchantments;
}

} // namespace

int main() {
    try {
        const platform::MCE platform = platform::MCE::Java;
        const EnchInfoList enchantment_info = {
            {
                "sharpness",
                "Sharpness",
                platform,
                5,
                5,
                1,
                {},
                {EquipmentCategory::Sword()},
            },
            {
                "looting",
                "Looting",
                platform,
                3,
                3,
                4,
                {},
                {EquipmentCategory::Sword()},
            },
            {
                "smite",
                "Smite",
                platform,
                5,
                5,
                1,
                {"sharpness"},
                {EquipmentCategory::Sword()},
            },
        };

        EnchInfo::initialize(enchantment_info);
        EnchInfo::set_active_platform(platform);

        const int32_t sharpness_id = EnchInfo::get_id("sharpness");
        const int32_t looting_id   = EnchInfo::get_id("looting");

        if (sharpness_id < 0 || looting_id < 0)
            throw std::runtime_error("Demo enchantments were not initialized");

        const EquipmentType sword = {
            "diamond_sword",
            "Diamond Sword",
            EquipmentCategory::Sword(),
            1561,
        };

        const ItemStack target_item(
            &sword,
            EnchSet{
                Ench(sharpness_id, 5),
                Ench(looting_id, 3),
            },
            0
        );

        const ItemStack base_item(&sword, EnchSet{}, 0);
        const ItemStack sharpness_book_a(EnchSet{Ench(sharpness_id, 4)});
        const ItemStack sharpness_book_b(EnchSet{Ench(sharpness_id, 4)});
        const ItemStack looting_book(EnchSet{Ench(looting_id, 3)});
        const ItemCollection available_items = {
            base_item,
            sharpness_book_a,
            sharpness_book_b,
            looting_book,
        };

        EnchStepList steps;
        steps.reserve(3);

        const auto [step1_result, step1_level_cost] = Utils::forge_item(base_item, sharpness_book_a);
        steps.push_back({
            base_item,
            sharpness_book_a,
            step1_level_cost,
            Utils::calc_exp(step1_level_cost),
        });

        const auto [step2_result, step2_level_cost] = Utils::forge_item(step1_result, sharpness_book_b);
        steps.push_back({
            step1_result,
            sharpness_book_b,
            step2_level_cost,
            Utils::calc_exp(step2_level_cost),
        });

        const auto [final_item, step3_level_cost] = Utils::forge_item(step2_result, looting_book);
        steps.push_back({
            step2_result,
            looting_book,
            step3_level_cost,
            Utils::calc_exp(step3_level_cost),
        });

        const bool is_success = matches_target(final_item, target_item);
        const EnchSolution solution = EnchSolution::make(
            platform,
            base_item.enchantments,
            target_item,
            available_items,
            steps,
            is_success,
            {
                "demo-sequence",
                "0.1.0",
                0,
                0,
            }
        );

        if (!solution.is_feasible()) {
            std::cerr << "Demo sequence did not produce the target item." << std::endl;
            return 1;
        }

        std::cout << "BestEnchSeq demo" << std::endl;
        std::cout << "Platform: " << platform_name(solution.platform) << std::endl;
        std::cout << "Equipment: " << sword.name << std::endl;
        std::cout << "Target: " << join(describe_enchantments(target_item.enchantments), ", ") << std::endl;
        std::cout << "Inputs:" << std::endl;
        for (const ItemStack &item : available_items) {
            std::cout << "  - " << describe_item(item) << std::endl;
        }

        std::cout << "Steps:" << std::endl;
        for (size_t i = 0; i < solution.steps.size(); ++i) {
            const auto &step = solution.steps[i];
            const auto [result_item, ignored_level_cost] = Utils::forge_item(step.item_a, step.item_b);
            (void)ignored_level_cost;
            std::cout << "  " << (i + 1) << ". " << describe_item(step.item_a) << " + "
                      << describe_item(step.item_b) << " -> " << describe_item(result_item)
                      << "  levels=" << step.exp_level_cost << " exp=" << step.exp_cost << std::endl;
        }

        std::cout << "Result:" << std::endl;
        std::cout << "  final=" << describe_item(final_item) << std::endl;
        std::cout << "  total_levels=" << solution.total_exp_level_cost << std::endl;
        std::cout << "  total_exp=" << solution.total_exp_cost << std::endl;
        std::cout << "  peak_step_levels=" << solution.get_peek_level_cost() << std::endl;

        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Failed to run demo: " << exception.what() << std::endl;
        return 1;
    }
}
