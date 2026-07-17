#include "framework/test_utils.h"
#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include <memory>

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

int main() {
    try {
        test_serializer_name();
        test_serializer_interface();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
