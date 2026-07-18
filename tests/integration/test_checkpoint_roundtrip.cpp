#include "framework/test_utils.h"
#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include "algorithm/strategies/astar/AStarAlgorithm.h"
#include "algorithm/serialization/IAlgorithmSerializer.h"
#include "algorithm/serialization/CompactSerializer.h"
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

void test_checkpoint_serialize_roundtrip() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    auto checkpoint = ser.serialize(algo);
    expect(!checkpoint.empty(), "serialize should produce non-empty checkpoint");
    AStarAlgorithm algo2;
    bool ok = ser.deserialize(algo2, checkpoint);
    expect(ok, "deserialize should return true for valid checkpoint");
    TEST_PASS("test_checkpoint_serialize_roundtrip");
}

void test_checkpoint_empty_rejected() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    bool ok = ser.deserialize(algo, std::span<const uint8_t>());
    expect(!ok, "empty checkpoint rejected");
    TEST_PASS("test_checkpoint_empty_rejected");
}

void test_checkpoint_bad_magic_rejected() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    uint8_t trash[10] = {};
    bool ok = ser.deserialize(algo, trash);
    expect(!ok, "bad magic rejected");
    TEST_PASS("test_checkpoint_bad_magic_rejected");
}

void test_checkpoint_wrong_tag_rejected() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    ByteStreamWriter w;
    w.u32(compact_serial::FILE_MAGIC);
    w.u16(compact_serial::FILE_VERSION);
    w.u16(0);
    w.u32(0);
    w.i64(0);
    uint8_t zero_crc[7] = {};
    w.bytes(zero_crc, 7);
    w.u16(1);
    const char* bad = "WRONG_ALGO";
    w.u8(static_cast<uint8_t>(std::strlen(bad)));
    w.bytes(bad, std::strlen(bad));
    auto buf = std::move(w).take();
    bool ok = ser.deserialize(algo, std::span<const uint8_t>(buf.data(), buf.size()));
    expect(!ok, "wrong tag rejected");
    TEST_PASS("test_checkpoint_wrong_tag_rejected");
}

void test_checkpoint_extra_data_rejected() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    auto checkpoint = ser.serialize(algo);
    std::vector<uint8_t> corrupted(checkpoint.begin(), checkpoint.end());
    corrupted.push_back(0xFF);
    AStarAlgorithm algo2;
    bool ok = ser.deserialize(algo2, corrupted);
    expect(!ok, "trailing garbage rejected");
    TEST_PASS("test_checkpoint_extra_data_rejected");
}

int main() {
    try {
        test_checkpoint_serialize_roundtrip();
        test_checkpoint_empty_rejected();
        test_checkpoint_bad_magic_rejected();
        test_checkpoint_wrong_tag_rejected();
        test_checkpoint_extra_data_rejected();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
