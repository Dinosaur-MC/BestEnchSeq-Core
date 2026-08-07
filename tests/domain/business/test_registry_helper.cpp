#define BESQ_TEST_MAIN
#include "domain/business/components/RegistryHelper.h"
#include "framework/test_framework.h"

namespace {

Profile make_p(const std::string& name, int base_level) {
    Profile p(name);
    p.add_enchantment({NSID("minecraft:sharpness"),
                       "Sharpness",
                       MCE::All,
                       base_level,
                       base_level,
                       1,
                       false,
                       {},
                       {NSID("#minecraft:swords")}});
    return p;
}

Profile make_p2(const std::string& name, std::initializer_list<NSID> ids) {
    Profile p(name);
    for (const auto& id : ids)
        p.add_enchantment({id, id.str(), MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    return p;
}

} // namespace

TEST_CASE("test_registry_helper_set_ops") {
    auto a = make_p2("a", {NSID("minecraft:sharpness"), NSID("minecraft:knockback")});
    auto b = make_p2("b", {NSID("minecraft:knockback"), NSID("minecraft:smite")});

    auto u = RegistryHelper::unite("u", a, b);
    expect(u.ench().contains(NSID("minecraft:sharpness")), "unite has sharpness (a-only)");
    expect(u.ench().contains(NSID("minecraft:knockback")), "unite has knockback (shared)");
    expect(u.ench().contains(NSID("minecraft:smite")), "unite has smite (b-only)");
    expect_eq(u.ench().size(), 3u, "unite dedups shared entries");

    auto i = RegistryHelper::intersect("i", a, b);
    expect(i.ench().contains(NSID("minecraft:knockback")), "intersect has the common entry");
    expect(!i.ench().contains(NSID("minecraft:sharpness")), "intersect drops a-only");
    expect_eq(i.ench().size(), 1u, "intersect keeps only the common entry");

    auto s = RegistryHelper::subtract("s", a, b);
    expect(s.ench().contains(NSID("minecraft:sharpness")), "subtract keeps a-only");
    expect(!s.ench().contains(NSID("minecraft:knockback")), "subtract removes shared");
    expect_eq(s.ench().size(), 1u, "subtract removes shared + b-only");
    TEST_PASS("registry_helper set ops");
}

TEST_CASE("test_registry_helper_diff") {
    auto a = make_p2("a", {NSID("minecraft:sharpness"), NSID("minecraft:knockback")});
    auto b = make_p2("b", {NSID("minecraft:knockback"), NSID("minecraft:smite")});
    auto d = RegistryHelper::diff(a, b);

    bool removed_sharp = false, added_smite = false;
    for (const auto& e : d.enchantments) {
        if (e.id == NSID("minecraft:sharpness") && e.status == RegistryHelper::DiffEntry::Removed)
            removed_sharp = true;
        if (e.id == NSID("minecraft:smite") && e.status == RegistryHelper::DiffEntry::Added)
            added_smite = true;
    }
    expect(removed_sharp, "diff: sharpness (a-only) marked Removed");
    expect(added_smite, "diff: smite (b-only) marked Added");
    TEST_PASS("registry_helper diff");
}

TEST_CASE("test_registry_helper_operators") {
    auto a = make_p2("a", {NSID("minecraft:sharpness")});
    auto b = make_p2("b", {NSID("minecraft:smite")});

    auto u = a | b;
    expect(u.ench().contains(NSID("minecraft:sharpness")), "operator| unites a");
    expect(u.ench().contains(NSID("minecraft:smite")), "operator| unites b");

    auto i = a & b;
    expect_eq(i.ench().size(), 0u, "operator& empty for disjoint profiles");

    auto sub = a - b;
    expect(sub.ench().contains(NSID("minecraft:sharpness")), "operator- subtracts");

    auto m = a + b;
    expect(m.ench().contains(NSID("minecraft:smite")), "operator+ merges b in");
    TEST_PASS("registry_helper operators");
}

TEST_CASE("test_registry_helper_builder") {
    auto a = make_p2("a", {NSID("minecraft:sharpness")});
    auto b = make_p2("b", {NSID("minecraft:smite")});

    RegistryHelper builder;
    auto result = builder.load(a).unite(b).build("result");
    expect(result.ench().contains(NSID("minecraft:sharpness")), "builder chain unite has sharpness");
    expect(result.ench().contains(NSID("minecraft:smite")), "builder chain unite has smite");

    // filter predicate
    RegistryHelper fbuilder;
    auto filtered = fbuilder.load(a)
                        .filter([](const EnchInfo& e) {
                            return e.id == NSID("minecraft:smite"); // not present in a → empty
                        })
                        .build("f");
    expect_eq(filtered.ench().size(), 0u, "builder filter drops everything");
    TEST_PASS("registry_helper builder chain");
}

TEST_CASE("test_registry_helper_merge_override") {
    auto a = make_p("a", 5);
    auto b = make_p("b", 6);
    auto m = RegistryHelper::merge("m", a, b);
    expect(m.ench().at(NSID("minecraft:sharpness")).max_level == 6, "merge overwrites (b wins)");
    TEST_PASS("test_registry_helper_merge_override");
}

TEST_CASE("test_registry_helper_validate") {
    Profile p("v");
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    // smite exclusive-refs sharpness (exists) → valid
    p.add_enchantment({NSID("minecraft:smite"),
                       "Smite",
                       MCE::All,
                       5,
                       5,
                       1,
                       false,
                       {NSID("minecraft:sharpness")},
                       {NSID("#minecraft:swords")}});
    expect(RegistryHelper::validate(p), "valid profile passes");
    // bad: exclusive refs a nonexistent enchant
    Profile bad("bad");
    bad.add_enchantment(
        {NSID("minecraft:x"), "X", MCE::All, 1, 1, 1, false, {NSID("minecraft:missing")}, {NSID("#minecraft:swords")}});
    expect(!RegistryHelper::validate(bad), "exclusive ref to missing enchant fails");
    TEST_PASS("test_registry_helper_validate");
}

TEST_CASE("test_registry_helper_validate_max_level") {
    // max_level < 1 is invalid regardless of exclusive refs
    Profile p("ml");
    p.add_enchantment({NSID("minecraft:y"), "Y", MCE::All, 0, 0, 1, false, {}, {NSID("#minecraft:swords")}});
    expect(!RegistryHelper::validate(p), "max_level < 1 fails validation");
    TEST_PASS("test_registry_helper_validate_max_level");
}
