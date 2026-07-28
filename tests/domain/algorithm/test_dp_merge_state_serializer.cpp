#include "framework/test_utils.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeStateSerializer.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/serialization/Checkpoint.h"
#include <memory>
#include <span>
using namespace algorithm;

void test_dp_merge_serializer_name() {
    DPMergeStateSerializer ser;
    expect(ser.algorithm_name() == "dp_merge", "algorithm_name should be dp_merge");
    expect(ser.algorithm_version() == "1.0.0", "algorithm_version should be 1.0.0");
    TEST_PASS("test_dp_merge_serializer_name");
}

void test_dp_merge_serializer_interface() {
    auto ser = std::make_unique<DPMergeStateSerializer>();
    auto* base = dynamic_cast<IAlgorithmSerializer*>(ser.get());
    expect(base != nullptr, "DPMergeStateSerializer implements IAlgorithmSerializer");
    TEST_PASS("test_dp_merge_serializer_interface");
}

void test_dp_merge_roundtrip() {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;

    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    EnchSet target_set;
    target_set.insert(Ench{0, 1});
    input.target.enchs = target_set;
    input.ench_reg.init({}, {}, Equipment{});

    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    DPMergeAlgorithm algo2;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo2, out, blob);
    expect(ok, "round-trip deserialize should succeed");
    expect(out.target.type == ItemType::Equip, "target type should survive round-trip");
    TEST_PASS("test_dp_merge_roundtrip");
}

void test_dp_merge_tamper_detected() {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;

    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    input.ench_reg.init({}, {}, Equipment{});
    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    if (blob.size() > 10) {
        blob[blob.size() - 5] ^= 0xFF;
        DPMergeAlgorithm algo2;
        AlgorithmInput out;
        bool ok = ser.deserialize(algo2, out, blob);
        expect(!ok, "tampered checkpoint should be rejected by CRC");
    }
    TEST_PASS("test_dp_merge_tamper_detected");
}

void test_dp_merge_empty_rejected() {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>());
    expect(!ok, "empty checkpoint should return false");
    TEST_PASS("test_dp_merge_empty_rejected");
}

int main() {
    try {
        test_dp_merge_serializer_name();
        test_dp_merge_serializer_interface();
        test_dp_merge_roundtrip();
        test_dp_merge_tamper_detected();
        test_dp_merge_empty_rejected();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
