// =============================================================================
// WebSchema round-trip tests (reuse InventorySchema test pattern).
// =============================================================================
#define BESQ_TEST_MAIN
#include "domain/interface/web/WebSchema.h"
#include "ds/ds.h"
#include "framework/test_framework.h"
#include <string>

TEST_CASE("test_round_trip_full") {
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
    dto.ignore_incompatible = true;

    auto json = WebTaskJson::serialize(dto);
    expect(json["target"]["item"].as<std::string>() == "diamond_sword", "target serialized");
    expect(json["source"][0]["level"].as<int32_t>() == 2, "source serialized");
    expect(json["max_solutions"].as<int32_t>() == 3, "max_solutions serialized");
    expect(json["max_search_time"].as<int64_t>() == 5000, "max_search_time serialized");
    expect(json["max_threads"].as<int32_t>() == 4, "max_threads serialized");
    expect(json["ignore_incompatible"].as<bool>() == true, "ignore_incompatible serialized");

    WebTaskDto out;
    WebTaskJson::parse_or_throw(json, out);
    expect(out.algorithm == "dp_merge", "algorithm round-trips");
    expect(out.profile == "builtin:vanilla", "profile round-trips");
    expect(out.target.item == "diamond_sword", "target round-trips");
    expect(out.target.enchants.size() == 2, "target enchants round-trip");
    expect(out.items.size() == 1, "items round-trip");
    expect(out.items[0].type == "book", "item type round-trips");
    expect(out.source.size() == 1 && out.source[0].id == "sharpness", "source round-trips");
    expect(out.max_solutions == 3, "max_solutions round-trips");
    expect(out.max_search_time_ms == 5000, "max_search_time round-trips");
    expect(out.max_threads == 4, "max_threads round-trips");
    expect(out.ignore_incompatible == true, "ignore_incompatible round-trips");
    TEST_PASS("WebSchema full round-trip");
}

TEST_CASE("test_inv_task_payload_is_valid_webschema") {
    // A plain InvTaskSchema payload (no source/search fields) must parse.
    auto json = Json::parse(R"({
        "profile": "builtin:vanilla",
        "name": "task",
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
    expect(dto.max_search_time_ms == 0, "max_search_time defaults 0");
    expect(dto.max_threads == 0, "max_threads defaults 0");
    expect(dto.ignore_incompatible == false, "ignore_incompatible defaults false");
    TEST_PASS("InvTaskSchema payload is a valid WebSchema");
}
