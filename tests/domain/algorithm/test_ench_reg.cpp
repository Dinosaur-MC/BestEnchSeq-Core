#include "framework/test_utils.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

struct TestFixture {
    EnchantmentRegistry enchants;
    EquipmentTagRegistry categories;
    algorithm::EnchReg reg;

    TestFixture() {
        enchants = EnchantmentRegistry({
            {
                NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
                1, false,
                std::unordered_set<NSID>{},
                std::unordered_set<NSID>{EquipmentTag::sword()}
            },
            {
                NSID("knockback"), "Knockback", MCE::All, 2, 2,
                2, false,
                std::unordered_set<NSID>{},
                std::unordered_set<NSID>{EquipmentTag::sword()}
            },
            {
                NSID("bane_of_arthropods"), "Bane of Arthropods", MCE::All, 5, 5,
                1, false,
                std::unordered_set<NSID>{NSID("sharpness")},
                std::unordered_set<NSID>{EquipmentTag::sword()}
            },
            {
                NSID("protection"), "Protection", MCE::All, 4, 4,
                1, false,
                std::unordered_set<NSID>{},
                std::unordered_set<NSID>{EquipmentTag::chestplate()}
            },
        });

        algorithm::Equipment target_equip;
        target_equip.id = 0;
        target_equip.category_id = 0;
        target_equip.max_durability = 1561;
        // Sort by NSID for deterministic local ID assignment
        std::vector<std::pair<std::string, EnchInfo>> sorted_enchs;
        sorted_enchs.reserve(enchants.size());
        for (const auto& [nsid, info] : enchants.data())
            sorted_enchs.emplace_back(nsid.str(), info);
        std::sort(sorted_enchs.begin(), sorted_enchs.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<algorithm::EnchInfo> compact_infos;
        std::vector<NSID> global_ids;
        // Map enchantment name → local id for conflict resolution
        std::unordered_map<std::string, int32_t> name_to_local;

        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            NSID nsid(sorted_enchs[i].first);
            global_ids.push_back(nsid);
            name_to_local[sorted_enchs[i].first] = i;
        }

        size_t mask_size = (enchants.size() + 63) / 64;
        std::vector<std::vector<algorithm::MaskType>> exc_masks(enchants.size(),
            std::vector<algorithm::MaskType>(mask_size, 0));
        // Build conflict masks using shared group bits.
        uint64_t next_group = 0;
        std::vector<bool> visited(enchants.size(), false);
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            if (visited[i] || sorted_enchs[i].second.exclusive_set.empty()) continue;
            uint64_t group_bit = algorithm::MaskType(1) << (next_group % 64);
            next_group++;
            visited[i] = true;
            exc_masks[i][0] |= group_bit;
            for (const auto& ex_nsid : sorted_enchs[i].second.exclusive_set) {
                auto it = name_to_local.find(ex_nsid.str());
                if (it != name_to_local.end()) {
                    int32_t j = it->second;
                    visited[j] = true;
                    exc_masks[j][0] |= group_bit;
                }
            }
        }
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            const auto& ei = sorted_enchs[i].second;
            bool applicable = ei.applicable_equipments.count(EquipmentTag::sword()) > 0;
            algorithm::EnchInfo info;
            info.mul = static_cast<uint16_t>(ei.multiplier);
            info.mul_b = static_cast<uint16_t>(ei.multiplier);
            info.max_lvl = static_cast<uint16_t>(ei.max_level);
            info.exc_mask = exc_masks[i];
            info.applicable = applicable;
            compact_infos.push_back(std::move(info));
        }
        reg.init(compact_infos, global_ids, target_equip);
    }
};

void test_basic_init_and_size() {
    TestFixture fx;

    expect(fx.reg.size() == 4, "size: should have 4 enchantments");
    expect(fx.reg.get_target_equip().id == 0,
           "target: should be equipment id 0");

    std::cout << "PASS: test_basic_init_and_size" << std::endl;
}

void test_safe_get_bounds() {
    TestFixture fx;

    expect(fx.reg.get(0).mul > 0, "get(0): multiplier should be > 0");

    bool threw = false;
    try {
        (void)fx.reg.get(static_cast<int16_t>(fx.reg.size()));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(): should throw on out-of-range id");

    threw = false;
    try {
        (void)fx.reg.get(static_cast<int16_t>(-1));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(): should throw on negative id");

    std::cout << "PASS: test_safe_get_bounds" << std::endl;
}

void test_conflict_detection() {
    TestFixture fx;

    // After NSID-sorted ordering:
    //   0 = bane_of_arthropods, 1 = knockback, 2 = protection, 3 = sharpness
    // bane(0) and sharpness(3) should conflict
    expect(fx.reg.is_conflict(0, 3), "conflict: bane vs sharpness should conflict");
    expect(fx.reg.is_conflict(3, 0), "conflict: sharpness vs bane should conflict (symmetric)");

    // knockback(1) and bane(0) should NOT conflict
    expect(!fx.reg.is_conflict(0, 1), "conflict: bane vs knockback should NOT conflict");

    // Self-check should NOT conflict
    expect(!fx.reg.is_conflict(0, 0), "conflict: self should NOT conflict");
    expect(!fx.reg.is_conflict(1, 1), "conflict: self should NOT conflict");

    std::cout << "PASS: test_conflict_detection" << std::endl;
}

void test_multiplier_and_max_level() {
    TestFixture fx;

    // sharpness: mult=1, max_lvl=5
    expect(fx.reg[0].mul == 1, "multiplier: sharpness should be 1");
    expect(fx.reg[0].max_lvl == 5, "max_level: sharpness should be 5");

    // knockback: mult=2, max_lvl=2
    expect(fx.reg[1].mul == 2, "multiplier: knockback should be 2");
    expect(fx.reg[1].max_lvl == 2, "max_level: knockback should be 2");

    std::cout << "PASS: test_multiplier_and_max_level" << std::endl;
}

// ─── algorithm::EnchSet dedicated tests ────────────────────────────

void test_enchset_hash_consistency() {
    algorithm::EnchSet set;
    set.insert({0, 5});
    set.insert({1, 2});

    auto h1 = set.hash();
    auto h2 = set.hash();
    expect(h1 == h2, "enchset hash: same set should produce same hash");

    algorithm::EnchSet set2;
    set2.insert({0, 3});
    set2.insert({1, 2});
    auto h3 = set2.hash();
    expect(h1 != h3, "enchset hash: different levels should differ");

    algorithm::EnchSet set3;
    set3.insert({0, 5});
    set3.insert({2, 2});
    auto h4 = set3.hash();
    expect(h1 != h4, "enchset hash: different ids should differ");

    std::cout << "PASS: test_enchset_hash_consistency" << std::endl;
}

void test_enchset_sort_restores_invariant() {
    algorithm::EnchSet set;
    set.insert({0, 5});
    set.insert({2, 3});

    // Mutate through mutable iterator to break sorting
    auto it = set.begin();
    std::swap(*it, *(it + 1));

    (void)set.find(2);
    set.sort();

    auto after = set.find(2);
    expect(after != set.end() && after->level == 3,
           "enchset sort: find(2) should work after sort");
    auto after0 = set.find(0);
    expect(after0 != set.end() && after0->level == 5,
           "enchset sort: find(0) should work after sort");

    std::cout << "PASS: test_enchset_sort_restores_invariant" << std::endl;
}

void test_enchset_empty_and_single() {
    algorithm::EnchSet empty;
    expect(empty.size() == 0, "enchset empty: size 0");
    expect(empty.empty(), "enchset empty: empty() true");
    expect(empty.find(0) == empty.end(), "enchset empty: find returns end");

    algorithm::EnchSet single;
    single.insert({3, 1});
    expect(single.size() == 1, "enchset single: size 1");
    expect(single.contains(3), "enchset single: contains 3");
    expect(!single.contains(0), "enchset single: not contains 0");

    single.insert({3, 2});
    expect(single.size() == 1, "enchset single: update same id, size still 1");
    auto it = single.find(3);
    expect(it != single.end() && it->level == 2,
           "enchset single: find(3) level updated to 2");

    std::cout << "PASS: test_enchset_empty_and_single" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        test_basic_init_and_size();
        test_safe_get_bounds();
        test_conflict_detection();
        test_multiplier_and_max_level();
        test_enchset_hash_consistency();
        test_enchset_sort_restores_invariant();
        test_enchset_empty_and_single();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
