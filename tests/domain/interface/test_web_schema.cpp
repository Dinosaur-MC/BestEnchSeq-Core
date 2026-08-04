// =============================================================================
// WebSchema round-trip tests (reuse InventorySchema test pattern).
// =============================================================================
#include "domain/interface/web/WebSchema.h"
#include "ds/ds.h"
#include "framework/test_utils.h"
#include <string>

void test_round_trip_full() {
    WebTaskDto dto;
    dto.target.item = "diamond_sword";
    dto.target.enchants = {{"sharpness", 5}, {"knockback", 2}};
    dto.items = {{"book", "", {{"sharpness", 5}}, 0, 1, 0}};
    dto.algorithm = "dp_merge";
    dto.profile = "builtin:vanilla";
    dto.source = {{"sharpness", 2}};
    dto.max_solutions = 3;
    dto.max_search_time_ms = 5000;
    dto.max_threads = 4;

    auto json = WebTaskJson::serialize(dto);
    expect(json["target"]["item"].as<std::string>() == "diamond_sword", "target serialized");
    expect(json["source"][0]["level"].as<int32_t>() == 2, "source serialized");
    expect(json["max_solutions"].as<int32_t>() == 3, "max_solutions serialized");

    WebTaskDto out;
    WebTaskJson::parse_or_throw(json, out);
    expect(out.target.item == "diamond_sword", "target round-trips");
    expect(out.target.enchants.size() == 2, "target enchants round-trip");
    expect(out.items.size() == 1, "items round-trip");
    expect(out.items[0].type == "book", "item type round-trips");
    expect(out.source.size() == 1 && out.source[0].id == "sharpness", "source round-trips");
    expect(out.max_solutions == 3, "max_solutions round-trips");
    expect(out.max_search_time_ms == 5000, "max_search_time round-trips");
    expect(out.max_threads == 4, "max_threads round-trips");
    TEST_PASS("WebSchema full round-trip");
}

void test_inv_task_payload_is_valid_webschema() {
    // A plain InvTaskSchema payload (no source/search fields) must parse.
    auto json = Json::parse(R"({
        "profile": "builtin:vanilla",
        "target": { "item": "diamond_sword", "enchants": [{"id":"sharpness","level":5}] },
        "items": [ { "type":"book", "enchants":[{"id":"sharpness","level":5}] } ],
        "algorithm": "dp_merge"
    })");
    WebTaskDto dto;
    WebTaskJson::parse_or_throw(json, dto);
    expect(dto.target.item == "diamond_sword", "target item parsed");
    expect(dto.algorithm == "dp_merge", "algorithm parsed");
    expect(dto.source.empty(), "source defaults empty");
    expect(dto.max_solutions == 0, "search fields default 0");
    TEST_PASS("InvTaskSchema payload is a valid WebSchema");
}

int main() {
    try {
        test_round_trip_full();
        test_inv_task_payload_is_valid_webschema();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
