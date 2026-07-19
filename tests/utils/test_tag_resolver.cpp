#include "framework/test_utils.h"
#include "resolvers/TagResolver.hpp"
#include "io/json.h"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace {

// ---------------------------------------------------------------------------
// Helper: create a temporary tag file (including parent directories)
// ---------------------------------------------------------------------------
void create_tag_file(const std::string &path, const std::string &content) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

// ---------------------------------------------------------------------------
// test_single_tag
// ---------------------------------------------------------------------------
void test_single_tag() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_tags_single";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_tags_single").string();
    create_tag_file(
        dir + "/data/minecraft/tags/enchantment/exclusive_set/weapon.json",
        R"({"values": ["minecraft:sharpness", "minecraft:smite"]})"
    );

    TagResolver resolver;
    resolver.load_from(dir);

    auto result = resolver.resolve("#minecraft:exclusive_set/weapon");
    expect(result.size() == 2, "weapon tag should have 2 entries");
    expect(result.contains("minecraft:sharpness"), "should contain sharpness");
    expect(result.contains("minecraft:smite"), "should contain smite");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_nested_tags
// ---------------------------------------------------------------------------
void test_nested_tags() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_tags_nested";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_tags_nested").string();
    create_tag_file(
        dir + "/data/minecraft/tags/enchantment/exclusive_set/weapon.json",
        R"({"values": ["minecraft:sharpness", "#minecraft:exclusive_set/undead"]})"
    );
    create_tag_file(
        dir + "/data/minecraft/tags/enchantment/exclusive_set/undead.json",
        R"({"values": ["minecraft:smite", "minecraft:bane_of_arthropods"]})"
    );

    TagResolver resolver;
    resolver.load_from(dir);

    auto result = resolver.resolve("#minecraft:exclusive_set/weapon");
    expect(result.size() == 3, "weapon tag should resolve nested refs");
    expect(result.contains("minecraft:sharpness"), "direct entry");
    expect(result.contains("minecraft:smite"), "nested via undead tag");
    expect(result.contains("minecraft:bane_of_arthropods"), "nested via undead tag");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_concrete_id_passthrough
// ---------------------------------------------------------------------------
void test_concrete_id_passthrough() {
    TagResolver resolver;
    auto result = resolver.resolve("minecraft:sharpness");
    expect(result.size() == 1, "concrete id returns set of 1");
    expect(result.contains("minecraft:sharpness"), "passthrough works");
}

// ---------------------------------------------------------------------------
// test_is_tag
// ---------------------------------------------------------------------------
void test_is_tag() {
    expect(TagResolver::is_tag("#minecraft:foo"), "starts with #");
    expect(!TagResolver::is_tag("minecraft:foo"), "no #");
    expect(!TagResolver::is_tag(""), "empty string");
}

// ---------------------------------------------------------------------------
// test_missing_tag_returns_empty
// ---------------------------------------------------------------------------
void test_missing_tag_returns_empty() {
    TagResolver resolver;
    auto result = resolver.resolve("#missing:tag");
    expect(result.empty(), "missing tag should return empty");
}

// ---------------------------------------------------------------------------
// test_cyclic_tag_detection
// ---------------------------------------------------------------------------
void test_cyclic_tag_detection() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_tags_cyclic";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_tags_cyclic").string();
    create_tag_file(
        dir + "/data/minecraft/tags/enchantment/cycle_a.json",
        R"({"values": ["#minecraft:cycle_b"]})"
    );
    create_tag_file(
        dir + "/data/minecraft/tags/enchantment/cycle_b.json",
        R"({"values": ["#minecraft:cycle_a"]})"
    );

    TagResolver resolver;
    resolver.load_from(dir);

    // Should not infinite-loop; should return empty
    auto result = resolver.resolve("#minecraft:cycle_a");
    expect(result.empty(), "cyclic tag should return empty or handled");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_resolve_vector
// ---------------------------------------------------------------------------
void test_resolve_vector() {
    TagResolver resolver;
    std::vector<std::string> refs = {"minecraft:sharpness", "minecraft:smite"};
    auto result = resolver.resolve(refs);
    expect(result.size() == 2, "vector resolve should return union");
    expect(result.contains("minecraft:sharpness"), "first element");
    expect(result.contains("minecraft:smite"), "second element");
}

// ---------------------------------------------------------------------------
// test_get_tag
// ---------------------------------------------------------------------------
void test_get_tag() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_tags_get_tag";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_tags_get_tag").string();
    create_tag_file(
        dir + "/data/minecraft/tags/enchantment/weapon.json",
        R"({"values": ["minecraft:sharpness"]})"
    );

    TagResolver resolver;
    resolver.load_from(dir);

    const auto *tag = resolver.get_tag("minecraft", "weapon");
    expect(tag != nullptr, "get_tag should find existing tag");
    expect(tag->size() == 1, "tag should have 1 entry");
    expect(tag->contains("minecraft:sharpness"), "tag should contain sharpness");

    const auto *missing = resolver.get_tag("minecraft", "nonexistent");
    expect(missing == nullptr, "get_tag should return nullptr for missing tag");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_item_tags
// ---------------------------------------------------------------------------
void test_item_tags() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_tags_item";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_tags_item").string();
    create_tag_file(
        dir + "/data/minecraft/tags/item/swords.json",
        R"({"values": ["minecraft:diamond_sword", "minecraft:iron_sword"]})"
    );

    TagResolver resolver;
    resolver.load_from(dir);

    auto result = resolver.resolve("#minecraft:swords");
    expect(result.size() == 2, "item tag should have 2 entries");
    expect(result.contains("minecraft:diamond_sword"), "should contain diamond_sword");
    expect(result.contains("minecraft:iron_sword"), "should contain iron_sword");

    std::filesystem::remove_all(temp_dir);
}

} // namespace

int main() {
    try {
        test_single_tag();
        test_nested_tags();
        test_concrete_id_passthrough();
        test_is_tag();
        test_missing_tag_returns_empty();
        test_cyclic_tag_detection();
        test_resolve_vector();
        test_get_tag();
        test_item_tags();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
