#include "test_utils.h"
#include "registries/AlgorithmRegistry.h"

class TestAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "test_algo"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>&,
        const compact::EnchReg&,
        const std::vector<compact::Ench>&,
        ExecutionContext&
    ) override {}
};

void test_registry() {
    AlgorithmRegistry::get_instance().register_algorithm("test", []{
        return std::make_unique<TestAlgorithm>();
    });

    expect(AlgorithmRegistry::get_instance().has_algorithm("test"), "test algo should be registered");
    expect(!AlgorithmRegistry::get_instance().has_algorithm("nonexistent"), "nonexistent algo should not be found");

    auto algo = AlgorithmRegistry::get_instance().create("test");
    expect(algo != nullptr, "created algo should not be null");
    expect(algo->name() == "test_algo", "name should match");

    auto missing = AlgorithmRegistry::get_instance().create("nonexistent");
    expect(missing == nullptr, "missing algo should return null");

    auto available = AlgorithmRegistry::get_instance().available_algorithms();
    expect(available.size() >= 1, "should have at least 1 algo");

    std::cout << "PASS: test_registry" << std::endl;
}

int main() {
    test_registry();
    std::cout << "All registry tests passed!" << std::endl;
    return 0;
}
