#define BESQ_TEST_MAIN
#include "astar/AStarAlgorithm.h"
#include "astar/AStarStateSerializer.h"
#include "dfs/DFSAlgorithm.h"
#include "domain/algorithm/serialization/Checkpoint.h"
#include "framework/test_framework.h"
#include <memory>
#include <span>
using namespace algorithm;

TEST_CASE("test_serializer_name") {
    AStarStateSerializer ser;
    expect(ser.algorithm_name() == "astar", "algorithm_name should be astar");
    expect(ser.algorithm_version() == "2.0.0", "algorithm_version should be 2.0.0");
    TEST_PASS("test_serializer_name");
}

TEST_CASE("test_serializer_interface") {
    auto ser = std::make_unique<AStarStateSerializer>();
    auto* base = dynamic_cast<IAlgorithmSerializer*>(ser.get());
    expect(base != nullptr, "AStarStateSerializer implements IAlgorithmSerializer");
    expect(base->algorithm_name() == "astar", "interface algorithm_name() works");
    expect(base->algorithm_version() == "2.0.0", "interface algorithm_version() works");
    TEST_PASS("test_serializer_interface");
}

TEST_CASE("test_astar_state_empty_rejected") {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>());
    expect(!ok, "empty checkpoint should return false");
    TEST_PASS("test_astar_state_empty_rejected");
}

TEST_CASE("test_astar_state_bad_magic_rejected") {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    uint8_t bad_data[8] = {0, 0, 0, 0, 1, 0, 0, 0};
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, bad_data);
    expect(!ok, "bad magic should return false");
    TEST_PASS("test_astar_state_bad_magic_rejected");
}

TEST_CASE("test_astar_state_bad_tag") {
    AStarStateSerializer ser;
    AStarAlgorithm algo;

    // Build a valid checkpoint with a wrong algorithm tag
    checkpoint::Checkpoint cp("WRONG_ALGO", 1);
    // Add a dummy input section so the checkpoint is structurally valid
    AlgorithmInput dummy;
    cp.add_section(checkpoint::SECTION_TYPE_INPUT, 0, dummy);

    ByteStreamWriter w;
    w << cp;
    auto buf = std::move(w).take();

    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>(buf.data(), buf.size()));
    expect(!ok, "checkpoint with a foreign algorithm tag must be rejected");
    TEST_PASS("test_astar_state_bad_tag");
}

TEST_CASE("test_cross_algorithm_checkpoint_rejected") {
    AStarStateSerializer ser;
    AStarAlgorithm astar;

    // Serialize a valid astar checkpoint (tag = "astar")
    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    EnchSet target_set;
    target_set.insert(Ench{0, 1});
    input.target.enchs = target_set;
    input.registry.init({}, {}, Equipment{});
    auto blob = ser.serialize(astar, input);
    expect(!blob.empty(), "serialize should produce bytes");

    // Deserialize the astar checkpoint into a DIFFERENT algorithm → rejected
    DFSAlgorithm dfs;
    AlgorithmInput out;
    bool ok = ser.deserialize(dfs, out, std::span<const uint8_t>(blob.data(), blob.size()));
    expect(!ok, "cross-algorithm checkpoint must be rejected");
    TEST_PASS("test_cross_algorithm_checkpoint_rejected");
}

TEST_CASE("test_crc_roundtrip") {
    AStarStateSerializer ser;
    AStarAlgorithm algo;

    // Serialize a real algorithm state
    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    EnchSet target_set;
    target_set.insert(Ench{0, 1});
    input.target.enchs = target_set;
    input.registry.init({}, {}, Equipment{});

    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    // Deserialize — should succeed with CRC verification
    AStarAlgorithm algo2;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo2, out, blob);
    expect(ok, "round-trip deserialize should succeed");
    expect(out.target.type == ItemType::Equip, "target type should survive round-trip");
    TEST_PASS("test_crc_roundtrip");
}

TEST_CASE("test_crc_tamper_detected") {
    AStarStateSerializer ser;
    AStarAlgorithm algo;

    AlgorithmInput input;
    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    // Tamper a byte in the payload region (past the header)
    if (blob.size() > 50) {
        blob[50] ^= 0xFF;
        AStarAlgorithm algo2;
        AlgorithmInput out;
        bool ok = ser.deserialize(algo2, out, blob);
        expect(!ok, "tampered checkpoint should be rejected by CRC");
    }
    TEST_PASS("test_crc_tamper_detected");
}

TEST_CASE("test_checkpoint_min_size_rejected") {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    AlgorithmInput out;

    // Data too small to contain even magic+version
    uint8_t tiny[5] = {0x42, 0x45, 0x53, 0x51, 0x01};
    bool ok = ser.deserialize(algo, out, tiny);
    expect(!ok, "5-byte data should be rejected");

    TEST_PASS("test_checkpoint_min_size_rejected");
}
