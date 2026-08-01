#include "framework/test_utils.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/algorithm/types/EnchSet.h"
#include "domain/algorithm/types/Enchantment.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

struct TestFixture {
    EnchantmentRegistry enchants;
    TagRegistry categories;
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
        target_equip.id = "test";
        target_equip.max_durability = 1561;
        // Sort by NSID for deterministic local ID assignment
        // Keep original NSID objects to ensure correct conflict matching.
        std::vector<std::pair<NSID, EnchInfo>> sorted_enchs;
        sorted_enchs.reserve(enchants.size());
        for (const auto& [nsid, info] : enchants.data())
            sorted_enchs.emplace_back(nsid, info);
        std::sort(sorted_enchs.begin(), sorted_enchs.end(),
            [](const auto& a, const auto& b) { return a.first.str() < b.first.str(); });

        std::vector<algorithm::EnchInfo> compact_infos;
        std::vector<NSID> global_ids;

        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            global_ids.push_back(sorted_enchs[i].first);
        }

        // Build exc_mask (ID-based): for each pair (i,j) where i's
        // exclusive_set contains j, set bit j in exc_masks[i] and vice versa.
        std::vector<algorithm::mask_type> exc_masks(sorted_enchs.size(), 0);
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            for (int32_t j = i + 1; j < static_cast<int32_t>(sorted_enchs.size()); ++j) {
                bool conflict = sorted_enchs[i].second.exclusive_set.count(sorted_enchs[j].first) ||
                                sorted_enchs[j].second.exclusive_set.count(sorted_enchs[i].first);
                if (conflict) {
                    exc_masks[i] |= (algorithm::mask_type(1) << j);
                    exc_masks[j] |= (algorithm::mask_type(1) << i);
                }
            }
        }
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            const auto& ei = sorted_enchs[i].second;
            bool applicable = ei.supported_items.count(EquipmentTag::sword()) > 0;
            algorithm::EnchInfo info;
            info.id = static_cast<uint8_t>(i);
            info.mul = static_cast<uint8_t>(ei.multiplier);
            info.mul_b = static_cast<uint8_t>(std::max(1, ei.multiplier >> 1));
            info.max_lvl = static_cast<uint8_t>(ei.max_level);
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
    expect(fx.reg.get_target_equip().id == NSID("test"),
           "target: should be equipment id test");

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

void test_enchset_mutable_iterator_write() {
    algorithm::EnchSet set;
    set.insert({0, 5});
    set.insert({2, 3});

    // Mutate level through mutable proxy
    auto it = set.begin();
    expect(it != set.end() && it->level() == 5, "first ench: id=0 level=5");

    ++it;
    expect(it != set.end() && it->id() == 2, "second ench: id=2");
    expect(it->level() == 3, "second ench: level=3");

    // Write through EnchRef::level() (mutable proxy)
    (*it).level() = 4;
    expect(it->level() == 4, "after write: level should be 4");

    // Verify hash is consistent after mutation
    auto h1 = set.hash();
    auto h2 = set.hash();
    expect(h1 == h2, "hash should be consistent after mutation");

    // Verify find still works after mutation
    expect(set.contains(2) && set[2] == 4,
           "find(2) should return updated level 4");
    expect(set.contains(0) && set[0] == 5,
           "find(0) should return unchanged level 5");

    std::cout << "PASS: test_enchset_mutable_iterator_write" << std::endl;
}

void test_enchset_empty_and_single() {
    algorithm::EnchSet empty;
    expect(empty.size() == 0, "enchset empty: size 0");
    expect(empty.empty(), "enchset empty: empty() true");
    expect(!empty.contains(0), "enchset empty: contains returns false for missing id");

    algorithm::EnchSet single;
    single.insert({3, 1});
    expect(single.size() == 1, "enchset single: size 1");
    expect(single.contains(3), "enchset single: contains 3");
    expect(!single.contains(0), "enchset single: not contains 0");

    single.insert({3, 2});
    expect(single.size() == 1, "enchset single: update same id, size still 1");
    expect(single.contains(3) && single[3] == 2,
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
        test_enchset_mutable_iterator_write();
        test_enchset_empty_and_single();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
