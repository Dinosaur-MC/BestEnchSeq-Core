#include "framework/test_utils.h"
#include "types/Ench.h"
#include "types/EnchSet.h"
#include "types/ItemStack.h"

// ─── Ench ───────────────────────────────────────────────────────────────

void test_ench_construct() {
    Ench e(5, 3);
    expect(e.id == 5, "ench id 5");
    expect(e.level == 3, "ench level 3");
    std::cout << "PASS: test_ench_construct" << std::endl;
}

void test_ench_default() {
    Ench e;
    expect(e.id == 0, "default id 0");
    expect(e.level == 1, "default level 1");
    std::cout << "PASS: test_ench_default" << std::endl;
}

void test_ench_equality() {
    Ench a(1, 2);
    Ench b(1, 2);
    Ench c(1, 3);
    expect(a == b, "same id+level");
    expect(!(a == c), "different level");
    expect(!(b == c), "different level");
    std::cout << "PASS: test_ench_equality" << std::endl;
}

void test_ench_hash() {
    Ench::Hash hasher;
    Ench a(3, 4);
    Ench b(3, 4);
    Ench c(4, 3);
    expect(hasher(a) == hasher(b), "same values → same hash");
    std::cout << "PASS: test_ench_hash" << std::endl;
}

// ─── EnchSet (domain) ───────────────────────────────────────────────────

void test_enchset_empty() {
    EnchSet s;
    expect(s.empty(), "default empty");
    expect(s.size() == 0, "size 0");
    std::cout << "PASS: test_enchset_empty" << std::endl;
}

void test_enchset_insert_and_find() {
    EnchSet s;
    s.emplace(3, 2);
    s.emplace(1, 5);
    s.emplace(7, 1);

    expect(s.size() == 3, "3 elements");
    expect(s.find_by_id(3) != s.end(), "find id 3");
    expect(s.find_by_id(1) != s.end(), "find id 1");
    expect(s.find_by_id(7) != s.end(), "find id 7");
    expect(s.find_by_id(99) == s.end(), "not find id 99");
    std::cout << "PASS: test_enchset_insert_and_find" << std::endl;
}

void test_enchset_erase() {
    EnchSet s;
    s.emplace(5, 1);
    s.emplace(2, 3);
    s.erase(Ench(5, 1));
    expect(s.size() == 1, "size 1 after erase");
    expect(s.find_by_id(2) != s.end(), "id 2 remains");
    expect(s.find_by_id(5) == s.end(), "id 5 gone");
    std::cout << "PASS: test_enchset_erase" << std::endl;
}

// ─── ItemStack ──────────────────────────────────────────────────────────

void test_itemstack_default() {
    ItemStack stack;
    expect(stack.enchantments.empty(), "default item has no enchants");
    expect(stack.prior_penalty == 0, "default penalty 0");
    std::cout << "PASS: test_itemstack_default" << std::endl;
}

void test_itemstack_book() {
    EnchSet enchants;
    enchants.emplace(1, 3);
    enchants.emplace(2, 2);
    ItemStack stack(enchants, 2);
    expect(stack.enchantments.size() == 2, "two enchants");
    expect(stack.prior_penalty == 2, "penalty 2");
    expect(stack.is_book(), "book item");
    expect(!stack.equipment.has_value(), "no equipment");
    std::cout << "PASS: test_itemstack_book" << std::endl;
}

// ─── Main ───────────────────────────────────────────────────────────────

int main() {
    try {
        test_ench_construct();
        test_ench_default();
        test_ench_equality();
        test_ench_hash();

        test_enchset_empty();
        test_enchset_insert_and_find();
        test_enchset_erase();

        test_itemstack_default();
        test_itemstack_book();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
