#include "framework/test_utils.h"
#include "dfs/DFSStateSerializer.h"
#include "dfs/DFSAlgorithm.h"
#include <memory>
#include <span>
using namespace algorithm;

void test_dfs_serializer_name() {
    DFSStateSerializer ser;
    expect(ser.algorithm_name() == "dfs", "algorithm_name should be dfs");
    expect(ser.algorithm_version() == "1.0.0", "algorithm_version should be 1.0.0");
    TEST_PASS("test_dfs_serializer_name");
}

void test_dfs_serializer_interface() {
    auto ser = std::make_unique<DFSStateSerializer>();
    auto* base = dynamic_cast<IAlgorithmSerializer*>(ser.get());
    expect(base != nullptr, "DFSStateSerializer implements IAlgorithmSerializer");
    TEST_PASS("test_dfs_serializer_interface");
}

void test_dfs_roundtrip() {
    DFSStateSerializer ser;
    DFSAlgorithm algo;

    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    EnchSet target_set;
    target_set.insert(Ench{0, 1});
    input.target.enchs = target_set;
    input.registry.init({}, {}, Equipment{});

    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    DFSAlgorithm algo2;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo2, out, blob);
    expect(ok, "round-trip deserialize should succeed");
    expect(out.target.type == ItemType::Equip, "target type should survive round-trip");
    TEST_PASS("test_dfs_roundtrip");
}

void test_dfs_tamper_detected() {
    DFSStateSerializer ser;
    DFSAlgorithm algo;

    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    input.registry.init({}, {}, Equipment{});
    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    if (blob.size() > 10) {
        blob[blob.size() - 5] ^= 0xFF;
        DFSAlgorithm algo2;
        AlgorithmInput out;
        bool ok = ser.deserialize(algo2, out, blob);
        expect(!ok, "tampered checkpoint should be rejected by CRC");
    }
    TEST_PASS("test_dfs_tamper_detected");
}

void test_dfs_empty_rejected() {
    DFSStateSerializer ser;
    DFSAlgorithm algo;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>());
    expect(!ok, "empty checkpoint should return false");
    TEST_PASS("test_dfs_empty_rejected");
}

int main() {
    try {
        test_dfs_serializer_name();
        test_dfs_serializer_interface();
        test_dfs_roundtrip();
        test_dfs_tamper_detected();
        test_dfs_empty_rejected();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
