#include "framework/test_utils.h"
#include "domain/business/types/Ench.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/types/Item.h"

#include <string>

// ─── Ench ───────────────────────────────────────────────────────────────

static const NSID& SHARPNESS() { static const NSID id("minecraft:sharpness"); return id; }
static const NSID& SMITE()     { static const NSID id("minecraft:smite");     return id; }
static const NSID& UNBREAKING(){ static const NSID id("minecraft:unbreaking");return id; }
static const NSID& BOOK()      { static const NSID id("minecraft:book");      return id; }

void test_ench_construct() {
    Ench e(SHARPNESS(), "Sharpness", 3);
    expect(e.id == SHARPNESS(), "ench id sharpness");
    expect(e.level == 3, "ench level 3");
    expect(e.name == "Sharpness", "ench name Sharpness");
    std::cout << "PASS: test_ench_construct" << std::endl;
}

void test_ench_default() {
    Ench e;
    expect(e.id.empty(), "default id empty");
    expect(e.level == 1, "default level 1");
    expect(e.name.empty(), "default name empty");
    std::cout << "PASS: test_ench_default" << std::endl;
}

void test_ench_equality() {
    Ench a(SHARPNESS(), "Sharpness", 2);
    Ench b(SHARPNESS(), "Sharpness", 2);
    Ench c(SHARPNESS(), "Sharpness", 3);
    expect(a == b, "same id+level");
    expect(!(a == c), "different level");
    expect(!(b == c), "different level");
    std::cout << "PASS: test_ench_equality" << std::endl;
}

void test_ench_hash() {
    Ench a(SHARPNESS(), "Sharpness", 4);
    Ench b(SHARPNESS(), "Sharpness", 4);
    Ench c(SMITE(), "Smite", 4);

    std::hash<Ench> hasher;
    expect(hasher(a) == hasher(b), "same values → same hash");
    // Different enchants at same level should have different hash (highly likely)
    expect(hasher(a) != hasher(c), "different enchants → different hash");
    std::cout << "PASS: test_ench_hash" << std::endl;
}

// ─── EnchSet ────────────────────────────────────────────────────────────

void test_enchset_empty() {
    EnchSet s;
    expect(s.empty(), "default empty");
    expect(s.size() == 0, "size 0");
    std::cout << "PASS: test_enchset_empty" << std::endl;
}

void test_enchset_insert_and_find() {
    EnchSet s;
    s.emplace(SHARPNESS(), "Sharpness", 2);
    s.emplace(SMITE(), "Smite", 5);
    s.emplace(UNBREAKING(), "Unbreaking", 3);

    expect(s.size() == 3, "3 elements");
    expect(s.find(SHARPNESS()) != s.end(), "find sharpness");
    expect(s.find(SMITE()) != s.end(), "find smite");
    expect(s.find(UNBREAKING()) != s.end(), "find unbreaking");
    expect(s.find(NSID("minecraft:unknown")) == s.end(), "not find unknown");
    std::cout << "PASS: test_enchset_insert_and_find" << std::endl;
}

void test_enchset_erase() {
    EnchSet s;
    s.emplace(SHARPNESS(), "Sharpness", 1);
    s.emplace(SMITE(), "Smite", 3);
    s.erase(Ench(SHARPNESS(), "Sharpness", 1));
    expect(s.size() == 1, "size 1 after erase");
    expect(s.find(SMITE()) != s.end(), "smite remains");
    expect(s.find(SHARPNESS()) == s.end(), "sharpness gone");
    std::cout << "PASS: test_enchset_erase" << std::endl;
}

// ─── Item ───────────────────────────────────────────────────────────────

void test_item_default() {
    Item stack;
    expect(stack.enchantments.empty(), "default item has no enchants");
    expect(stack.prior_penalty == 0, "default penalty 0");
    expect(!stack.is_book(), "default item is not a book");
    std::cout << "PASS: test_item_default" << std::endl;
}

void test_item_book() {
    EnchSet enchants;
    enchants.emplace(SHARPNESS(), "Sharpness", 3);
    enchants.emplace(SMITE(), "Smite", 2);
    Item stack(BOOK(), enchants, 2);
    expect(stack.enchantments.size() == 2, "two enchants");
    expect(stack.prior_penalty == 2, "penalty 2");
    expect(stack.is_book(), "book item");
    std::cout << "PASS: test_item_book" << std::endl;
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

        test_item_default();
        test_item_book();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
