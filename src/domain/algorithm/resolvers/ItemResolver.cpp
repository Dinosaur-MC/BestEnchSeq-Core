#include "ItemResolver.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace algorithm {

std::vector<std::string> check_applicability(
    const EnchSet &ench_set, const EnchSet &existing, const Item &target_item, const EnchReg &ench_reg
) {
    std::vector<std::string> errors;
    if (!target_item.equipment) {
        errors.push_back("target_item has no equipment");
        return errors;
    }

    auto check = [&](const Ench &e, const char *label) {
        const auto &info = ench_reg.get(e.id);
        bool ok          = false;
        for (int32_t cat_id : info.applicable_category_ids) {
            if (cat_id == target_item.equipment->category_id) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            errors.push_back(
                std::string(label) + " '" + info.name_id + "' is not applicable to '" +
                target_item.equipment->name_id + "'"
            );
        }
    };

    for (const auto &e : ench_set)
        check(e, "target_ench");
    for (const auto &e : existing)
        check(e, "existing_ench");
    return errors;
}

std::vector<std::string> check_conflicts(const EnchSet &ench_set, const EnchReg &ench_reg) {
    std::vector<std::string> errors;
    const auto &instances = ench_reg.get_instances();

    std::unordered_set<std::string> present;
    for (const auto &e : ench_set) {
        if (e.id >= 0 && e.id < static_cast<int32_t>(instances.size()))
            present.insert(instances[e.id].name_id);
    }

    for (const auto &e : ench_set) {
        if (e.id < 0 || e.id >= static_cast<int32_t>(instances.size()))
            continue;
        const auto &info = instances[e.id];
        for (const auto &excl : info.exclusive_set) {
            if (present.count(excl) && excl != info.name_id) {
                errors.push_back("enchantment '" + info.name_id + "' conflicts with '" + excl + "'");
            }
        }
    }

    return errors;
}

ResolvedInput ItemResolver::resolve(
    const Item &target_item, const EnchSet &source_ench, const EnchSet &target_ench,
    const EnchReg &ench_reg
) {
    // Step 1: Validate applicability
    auto app_errors = check_applicability(target_ench, source_ench, target_item, ench_reg);
    if (!app_errors.empty()) {
        std::string msg;
        for (const auto &err : app_errors)
            msg += err + "; ";
        msg.resize(msg.size() - 2);
        throw std::invalid_argument("ItemResolver: " + msg);
    }

    // Step 2: Validate conflicts (target_ench and source_ench combined)
    EnchSet combined = target_ench;
    for (const auto &e : source_ench) {
        auto it = target_ench.find_by_id(e.id);
        if (it == target_ench.end()) // only add if not already in target
            combined.emplace(e.id, e.level);
    }
    auto conf_errors = check_conflicts(combined, ench_reg);
    if (!conf_errors.empty()) {
        std::string msg;
        for (const auto &err : conf_errors)
            msg += err + "; ";
        msg.resize(msg.size() - 2);
        throw std::invalid_argument("ItemResolver: " + msg);
    }

    // Step 3: Compute diff = target_ench - source_ench
    EnchSet diff;
    for (const Ench &wanted : target_ench) {
        auto it                = source_ench.find_by_id(wanted.id);
        int32_t existing_level = (it != source_ench.end()) ? it->level : 0;
        if (existing_level < wanted.level) {
            diff.emplace(wanted.id, wanted.level);
        }
    }

    // Step 4: Generate graduated books for the diff
    ItemCollection books;
    for (const Ench &wanted : diff) {
        auto it                = source_ench.find_by_id(wanted.id);
        int32_t existing_level = (it != source_ench.end()) ? it->level : 0;
        for (int32_t lvl = existing_level + 1; lvl <= wanted.level; ++lvl) {
            EnchSet book_enchs;
            book_enchs.emplace(wanted.id, lvl);
            books.emplace_back(book_enchs, 0);
        }
    }

    return ResolvedInput{target_item, source_ench, target_ench, std::move(books)};
}
} // namespace algorithm
