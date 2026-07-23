#include "framework/test_utils.h"
#include "domain/algorithm/_strategies/astar/AStarStateSerializer.h"
#include "domain/algorithm/_strategies/astar/AStarAlgorithm.h"
#include "domain/algorithm/types/Checkpoint.h"
#include <memory>
#include <span>
using namespace algorithm;

void test_serializer_name() {
    AStarStateSerializer ser;
    expect(ser.algorithm_name() == "astar", "algorithm_name should be astar");
    expect(ser.algorithm_version() == "2.0.0", "algorithm_version should be 2.0.0");
    TEST_PASS("test_serializer_name");
}

void test_serializer_interface() {
    auto ser = std::make_unique<AStarStateSerializer>();
    auto* base = dynamic_cast<IAlgorithmSerializer*>(ser.get());
    expect(base != nullptr, "AStarStateSerializer implements IAlgorithmSerializer");
    expect(base->algorithm_name() == "astar", "interface algorithm_name() works");
    expect(base->algorithm_version() == "2.0.0", "interface algorithm_version() works");
    TEST_PASS("test_serializer_interface");
}

void test_astar_state_empty_rejected() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>());
    expect(!ok, "empty checkpoint should return false");
    TEST_PASS("test_astar_state_empty_rejected");
}

void test_astar_state_bad_magic_rejected() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;
    uint8_t bad_data[8] = {0, 0, 0, 0, 1, 0, 0, 0};
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, bad_data);
    expect(!ok, "bad magic should return false");
    TEST_PASS("test_astar_state_bad_magic_rejected");
}

void test_astar_state_bad_tag() {
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
    // The deserialize doesn't check algorithm_tag (it's metadata only),
    // so it will succeed since the structure is valid.
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>(buf.data(), buf.size()));
    expect(ok, "wrong algorithm tag with empty sections should still succeed (tag is metadata)");
    TEST_PASS("test_astar_state_bad_tag");
}

int main() {
    try {
        test_serializer_name();
        test_serializer_interface();
        test_astar_state_empty_rejected();
        test_astar_state_bad_magic_rejected();
        test_astar_state_bad_tag();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
