// =============================================================================
// Web API tests: BesqContext facade increments + per-resource handlers.
// =============================================================================
#include "domain/interface/BesqContext.h"
#include "framework/test_utils.h"
#include <filesystem>
#include <fstream>
#include <string>

// ── Facade increment: by-name profile read/write ──────────────────────────

void test_facade_by_name_registry() {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.fork_profile("builtin:vanilla", "mod:custom");

    // Read a non-active profile's registries without activating it.
    const auto& p = ctx.profile("mod:custom");
    expect(p.ench().contains(NSID("minecraft:sharpness")), "fork inherits enchants");
    expect(ctx.active_profile() == "builtin:vanilla", "read had no activation side effect");

    // Write to a non-active profile by name.
    EnchInfo info;
    info.id = NSID("mod:sword_ench");
    info.name = "Sword Enchantment";
    info.max_level = 5;
    info.multiplier = 2;
    info.supported_items.insert(NSID("#minecraft:swords"));
    expect(ctx.add_enchantment_to("mod:custom", info), "add enchantment by name");
    expect(ctx.profile("mod:custom").ench().contains(NSID("mod:sword_ench")), "ench visible by name");
    expect(!ctx.enchantments().contains(NSID("mod:sword_ench")), "active profile untouched");

    expect(ctx.remove_enchantment_from("mod:custom", NSID("mod:sword_ench")), "remove by name");
    expect(!ctx.profile("mod:custom").ench().contains(NSID("mod:sword_ench")), "ench gone by name");

    // Unknown profile read must throw.
    expect_throws([&] { ctx.profile("does:not_exist"); }, "unknown profile read throws");
    TEST_PASS("facade by-name registry");
}

int main() {
    try {
        test_facade_by_name_registry();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
