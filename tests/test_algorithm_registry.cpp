#include "test_utils.h"
#include "algorithm/AlgorithmRegistry.h"
#include "algorithm/DefaultForgeEngine.h"

class TestAlgorithm : public IAlgorithm {
public:
    TestAlgorithm() : _engine(ForgeConfig{}) {}
    std::string_view name() const noexcept override { return "test_algo"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _engine; }
    void execute(const AlgorithmInput&, ExecutionContext&) override {}
private:
    DefaultForgeEngine _engine;
};

void test_registry() {
    AlgorithmRegistry::instance().register_algorithm("test", []{
        return std::make_unique<TestAlgorithm>();
    });

    expect(AlgorithmRegistry::instance().has_algorithm("test"), "test algo should be registered");
    expect(!AlgorithmRegistry::instance().has_algorithm("nonexistent"), "nonexistent algo should not be found");

    auto algo = AlgorithmRegistry::instance().create("test");
    expect(algo != nullptr, "created algo should not be null");
    expect(algo->name() == "test_algo", "name should match");

    auto missing = AlgorithmRegistry::instance().create("nonexistent");
    expect(missing == nullptr, "missing algo should return null");

    auto available = AlgorithmRegistry::instance().available_algorithms();
    expect(available.size() >= 1, "should have at least 1 algo");

    std::cout << "PASS: test_registry" << std::endl;
}

int main() {
    test_registry();
    std::cout << "All registry tests passed!" << std::endl;
    return 0;
}
