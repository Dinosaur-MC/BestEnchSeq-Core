#include "common/CommonTypes.h"
#include "framework/test_utils.h"

#include <string>
#include <unordered_set>

namespace {

// ===========================================================================
// Default construction
// ===========================================================================

void test_default_construction() {
    NSID id;
    expect(id.empty(), "default NSID should be empty");
    expect(!id.is_tag(), "default NSID should not be a tag");
    expect_eq(id.get_ns(), "", "default NSID namespace should be empty");
    expect_eq(id.get_id(), "", "default NSID id should be empty");
    expect_eq(id.str(), "", "default NSID str() should be empty");

    std::cout << "  PASS: test_default_construction" << std::endl;
}

// ===========================================================================
// Two-arg construction (namespace + id)
// ===========================================================================

void test_two_arg_construction() {
    NSID id("minecraft", "sharpness");
    expect(!id.empty(), "two-arg NSID should not be empty");
    expect(!id.is_tag(), "two-arg NSID should not be a tag");
    expect_eq(id.get_ns(), "minecraft", "namespace should be minecraft");
    expect_eq(id.get_id(), "sharpness", "id should be sharpness");
    expect_eq(id.str(), "minecraft:sharpness", "str() should be 'minecraft:sharpness'");

    std::cout << "  PASS: test_two_arg_construction" << std::endl;
}

void test_two_arg_default_namespace() {
    // When ns is empty, it defaults to "minecraft"
    NSID id("", "sharpness");
    expect(!id.empty(), "NSID with default ns should not be empty");
    expect_eq(id.get_ns(), "minecraft", "empty ns defaults to minecraft");
    expect_eq(id.get_id(), "sharpness", "id should be sharpness");
    expect_eq(id.str(), "minecraft:sharpness", "str() should be 'minecraft:sharpness'");

    std::cout << "  PASS: test_two_arg_default_namespace" << std::endl;
}

void test_two_arg_tag_construction() {
    // ns with "#" prefix makes it a tag
    NSID id("#minecraft", "sword");
    expect(id.is_tag(), "NSID with # prefix should be a tag");
    expect_eq(id.get_ns(), "minecraft", "tag namespace without #");
    expect_eq(id.get_id(), "sword", "tag id should be sword");
    expect_eq(id.str(), "#minecraft:sword", "tag str() should include #");

    std::cout << "  PASS: test_two_arg_tag_construction" << std::endl;
}

// ===========================================================================
// String construction ("ns:id" format)
// ===========================================================================

void test_string_full() {
    NSID id("minecraft:sharpness");
    expect(!id.empty(), "string NSID should not be empty");
    expect(!id.is_tag(), "string NSID should not be a tag");
    expect_eq(id.get_ns(), "minecraft", "namespace should be minecraft");
    expect_eq(id.get_id(), "sharpness", "id should be sharpness");
    expect_eq(id.str(), "minecraft:sharpness", "str() should be 'minecraft:sharpness'");

    std::cout << "  PASS: test_string_full" << std::endl;
}

void test_string_bare_id() {
    // Bare "sharpness" defaults ns to "minecraft"
    NSID id("sharpness");
    expect(!id.empty(), "bare id NSID should not be empty");
    expect_eq(id.get_ns(), "minecraft", "bare id defaults to minecraft");
    expect_eq(id.get_id(), "sharpness", "id should be sharpness");
    expect_eq(id.str(), "minecraft:sharpness", "str() for bare id");

    std::cout << "  PASS: test_string_bare_id" << std::endl;
}

void test_string_tag() {
    NSID id("#minecraft:sword");
    expect(id.is_tag(), "tag string NSID should be a tag");
    expect_eq(id.get_ns(), "minecraft", "tag namespace");
    expect_eq(id.get_id(), "sword", "tag id");
    expect_eq(id.str(), "#minecraft:sword", "tag str()");

    std::cout << "  PASS: test_string_tag" << std::endl;
}

void test_string_invalid() {
    // Three tokens → invalid
    expect_throws([] { NSID("a:b:c"); }, "three-part string should throw");
    // Empty string → invalid
    expect_throws([] { NSID(""); }, "empty string should throw");

    std::cout << "  PASS: test_string_invalid" << std::endl;
}

// ===========================================================================
// Assignment from string
// ===========================================================================

void test_assignment_from_string() {
    NSID id;
    id = "minecraft:sharpness";
    expect_eq(id.get_ns(), "minecraft", "assigned namespace");
    expect_eq(id.get_id(), "sharpness", "assigned id");
    expect_eq(id.str(), "minecraft:sharpness", "assigned str()");

    // Re-assign to bare id
    id = "protection";
    expect_eq(id.get_ns(), "minecraft", "re-assigned namespace (defaulted)");
    expect_eq(id.get_id(), "protection", "re-assigned id");

    std::cout << "  PASS: test_assignment_from_string" << std::endl;
}

// ===========================================================================
// operator==
// ===========================================================================

void test_equality_same() {
    NSID a("minecraft", "sharpness");
    NSID b("minecraft", "sharpness");
    expect(a == b, "identical NSIDs should be equal");
    expect(!(a == NSID()), "non-empty NSID != empty NSID");
    expect(!(NSID() == a), "empty NSID != non-empty NSID");

    std::cout << "  PASS: test_equality_same" << std::endl;
}

void test_equality_different() {
    NSID a("minecraft", "sharpness");
    NSID b("minecraft", "protection");
    expect(!(a == b), "different ids should not be equal");

    NSID c("minecraft", "sharpness");
    NSID d("sharpness"); // bare id → "minecraft:sharpness"
    expect(c == d, "same ns:id constructed different ways should be equal");

    std::cout << "  PASS: test_equality_different" << std::endl;
}

void test_equality_tag_vs_non_tag() {
    NSID tag("#minecraft:sword");
    NSID plain("minecraft:sword");
    expect(!(tag == plain), "tag and non-tag with same ns:id should not be equal");

    std::cout << "  PASS: test_equality_tag_vs_non_tag" << std::endl;
}

void test_equality_empty() {
    NSID a;
    NSID b;
    expect(a == b, "two default NSIDs should be equal");

    std::cout << "  PASS: test_equality_empty" << std::endl;
}

// ===========================================================================
// unordered_set<NSID>
// ===========================================================================

void test_unordered_set() {
    std::unordered_set<NSID> ids;
    ids.insert(NSID("minecraft:sharpness"));
    ids.insert(NSID("minecraft:protection"));
    ids.insert(NSID("minecraft:sharpness")); // duplicate

    expect_eq(ids.size(), size_t(2), "unordered_set should deduplicate NSIDs");
    expect(ids.count(NSID("minecraft:sharpness")) == 1, "sharpness should be found");
    expect(ids.count(NSID("protection")) == 1, "protection (bare → minecraft:protection) should be found");
    expect(ids.count(NSID("minecraft:unbreaking")) == 0, "unbreaking should not be found");

    std::cout << "  PASS: test_unordered_set" << std::endl;
}

void test_unordered_set_with_tags() {
    std::unordered_set<NSID> ids;
    ids.insert(NSID("#minecraft:sword"));
    ids.insert(NSID("minecraft:sword")); // different from the tag version

    expect_eq(ids.size(), size_t(2), "tag and non-tag should both be stored");
    expect(ids.count(NSID("#minecraft:sword")) == 1, "tag should be found");
    expect(ids.count(NSID("minecraft:sword")) == 1, "non-tag should be found");

    std::cout << "  PASS: test_unordered_set_with_tags" << std::endl;
}

// ===========================================================================
// edge cases: valid and invalid IDs
// ===========================================================================

void test_valid_ids() {
    // Various valid NSID formats
    expect_eq(NSID("minecraft:sharpness").str(), "minecraft:sharpness", "basic id");
    expect_eq(NSID("minecraft:fire_aspect").str(), "minecraft:fire_aspect", "underscore in id");
    expect_eq(NSID("minecraft:_start_with_underscore").str(),
              "minecraft:_start_with_underscore",
              "id can start with underscore");
    expect_eq(NSID("somerig:custom_enchant").str(),
              "somerig:custom_enchant",
              "custom namespace");

    // Dots are allowed (MC identifier charset includes '.')
    expect_eq(NSID("minecraft:foo.bar").str(), "minecraft:foo.bar", "dot in id");
    expect_eq(NSID("foo.bar:thing").str(), "foo.bar:thing", "dot in namespace");
    expect_eq(NSID("custom:path/to/x.y").str(), "custom:path/to/x.y", "slash + dot in id");

    // Leading digits are allowed (MC has no positional rule)
    expect_eq(NSID("0abc:thing").str(), "0abc:thing", "leading digit in namespace");
    expect_eq(NSID("9minecraft:sharpness").str(), "9minecraft:sharpness", "leading digit ns");
    expect_eq(NSID("minecraft:1sharpness").str(), "minecraft:1sharpness", "leading digit id");
    expect_eq(NSID("minecraft:1x").str(), "minecraft:1x", "leading digit id");

    // '/' is allowed in the path (e.g. tag paths)
    expect_eq(NSID("#minecraft:enchantable/sharp_weapon").str(),
              "#minecraft:enchantable/sharp_weapon",
              "slash in tag path");

    std::cout << "  PASS: test_valid_ids" << std::endl;
}

void test_invalid_ids() {
    // Invalid characters
    expect_throws([] { NSID("minecraft:sharpness!"); }, "id with special chars should throw");
    expect_throws([] { NSID("minecraft:sharp ness"); }, "id with spaces should throw");

    std::cout << "  PASS: test_invalid_ids" << std::endl;
}

void test_mc_identifier_rules() {
    // Uppercase is rejected (MC identifiers are lowercase-only).
    expect_throws([] { NSID("Minecraft:sharpness"); }, "uppercase namespace should throw");
    expect_throws([] { NSID("minecraft:SharpNess"); }, "uppercase id should throw");

    // '/' is a path-only character: namespace must reject it.
    expect_throws([] { NSID("minecraft/enchantable:sharpness"); },
                  "slash in namespace should throw");

    // Filesystem-safety: '.' and '..' as whole segments are rejected.
    expect_throws([] { NSID("minecraft:."); }, "'.' id should throw");
    expect_throws([] { NSID("minecraft:.."); }, "'..' id should throw");
    expect_throws([] { NSID("minecraft:a/../b"); }, "'..' path segment should throw");
    expect_throws([] { NSID("minecraft:a/./b"); }, "'.' path segment should throw");

    // Embedded dots are fine: 'a..b' is a single segment, not '.', '..', or a separator.
    expect_eq(NSID("minecraft:a..b").str(), "minecraft:a..b", "embedded dots are allowed");
    expect_eq(NSID("minecraft:a.b/c").str(), "minecraft:a.b/c", "dot inside path segment");

    std::cout << "  PASS: test_mc_identifier_rules" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        // Default construction
        test_default_construction();

        // Two-arg construction
        test_two_arg_construction();
        test_two_arg_default_namespace();
        test_two_arg_tag_construction();

        // String construction
        test_string_full();
        test_string_bare_id();
        test_string_tag();
        test_string_invalid();

        // Assignment
        test_assignment_from_string();

        // Equality
        test_equality_same();
        test_equality_different();
        test_equality_tag_vs_non_tag();
        test_equality_empty();

        // unordered_set
        test_unordered_set();
        test_unordered_set_with_tags();

        // Edge cases
        test_valid_ids();
        test_invalid_ids();
        test_mc_identifier_rules();
    } catch (const test_error &e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return print_summary();
    } catch (const std::exception &e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return print_summary();
    }
    return print_summary();
}
