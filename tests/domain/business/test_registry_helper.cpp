#include "framework/test_utils.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/components/RegistryHelper.h"

namespace {

Profile make_p(const std::string& name, int base_level) {
    Profile p(name);
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, base_level, base_level, 1, false, {}, {NSID("#minecraft:swords")}});
    return p;
}

} // namespace

void test_registry_helper_merge_override() {
    auto a = make_p("a", 5);
    auto b = make_p("b", 6);
    auto m = RegistryHelper::merge("m", a, b);
    expect(m.ench().at(NSID("minecraft:sharpness")).max_level == 6, "merge overwrites (b wins)");
    TEST_PASS("test_registry_helper_merge_override");
}

void test_registry_helper_validate() {
    Profile p("v");
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    // smite exclusive-refs sharpness (exists) → valid
    p.add_enchantment({NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false, {NSID("minecraft:sharpness")}, {NSID("#minecraft:swords")}});
    expect(RegistryHelper::validate(p), "valid profile passes");
    // bad: exclusive refs a nonexistent enchant
    Profile bad("bad");
    bad.add_enchantment({NSID("minecraft:x"), "X", MCE::All, 1, 1, 1, false, {NSID("minecraft:missing")}, {NSID("#minecraft:swords")}});
    expect(!RegistryHelper::validate(bad), "exclusive ref to missing enchant fails");
    TEST_PASS("test_registry_helper_validate");
}

void test_registry_helper_validate_max_level() {
    // max_level < 1 is invalid regardless of exclusive refs
    Profile p("ml");
    p.add_enchantment({NSID("minecraft:y"), "Y", MCE::All, 0, 0, 1, false, {}, {NSID("#minecraft:swords")}});
    expect(!RegistryHelper::validate(p), "max_level < 1 fails validation");
    TEST_PASS("test_registry_helper_validate_max_level");
}

int main() {
    try {
        test_registry_helper_merge_override();
        test_registry_helper_validate();
        test_registry_helper_validate_max_level();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
