#include "framework/test_utils.h"
#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include <memory>

void test_serializer_tag() {
    AStarStateSerializer ser;
    expect(ser.tag() == "astar_v1", "tag should be astar_v1");
    TEST_PASS("test_serializer_tag");
}

void test_serializer_interface() {
    auto ser = std::make_unique<AStarStateSerializer>();
    auto* base = dynamic_cast<IAlgorithmSerializer*>(ser.get());
    expect(base != nullptr, "AStarStateSerializer implements IAlgorithmSerializer");
    expect(base->tag() == "astar_v1", "interface tag() works");
    TEST_PASS("test_serializer_interface");
}

int main() {
    try {
        test_serializer_tag();
        test_serializer_interface();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
