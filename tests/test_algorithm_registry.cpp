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

void test_basic_register_create() {
    auto& reg = AlgorithmRegistry::get_instance();
    reg.clear();

    reg.register_algorithm("test", []{ return std::make_unique<TestAlgorithm>(); });

    expect(reg.contains("test"), "contains: test algo should be registered");
    expect(!reg.contains("nonexistent"), "contains: nonexistent should not be found");

    auto algo = reg.create("test");
    expect(algo != nullptr, "create: created algo should not be null");
    expect(algo->name() == "test_algo", "create: name should match");

    auto missing = reg.create("nonexistent");
    expect(missing == nullptr, "create: missing algo should return null");

    std::cout << "PASS: test_basic_register_create" << std::endl;
}

void test_list_and_size() {
    auto& reg = AlgorithmRegistry::get_instance();
    reg.clear();

    expect(reg.size() == 0, "size: empty after clear");
    expect(reg.list().empty(), "list: empty after clear");

    reg.register_algorithm("algo_a", []{ return std::make_unique<TestAlgorithm>(); });
    reg.register_algorithm("algo_b", []{ return std::make_unique<TestAlgorithm>(); });

    expect(reg.size() == 2, "size: 2 after two registrations");
    auto list = reg.list();
    expect(list.size() == 2, "list: should have 2 entries");

    reg.clear();
    expect(reg.size() == 0, "size: 0 after clear");

    std::cout << "PASS: test_list_and_size" << std::endl;
}

void test_unregister() {
    auto& reg = AlgorithmRegistry::get_instance();
    reg.clear();

    reg.register_algorithm("temp_algo", []{ return std::make_unique<TestAlgorithm>(); });
    expect(reg.contains("temp_algo"), "unregister: algo exists before");

    reg.unregister_algorithm("temp_algo");
    expect(!reg.contains("temp_algo"), "unregister: algo gone after");
    expect(reg.size() == 0, "unregister: size 0");

    // Unregister nonexistent should not throw
    reg.unregister_algorithm("does_not_exist");

    std::cout << "PASS: test_unregister" << std::endl;
}

void test_deprecated_functions() {
    auto& reg = AlgorithmRegistry::get_instance();
    reg.clear();

    reg.register_algorithm("legacy", []{ return std::make_unique<TestAlgorithm>(); });

    expect(reg.has_algorithm("legacy"), "has_algorithm: legacy should work");
    expect(!reg.has_algorithm("ghost"), "has_algorithm: ghost not found");

    auto avail = reg.available_algorithms();
    expect(avail.size() == 1, "available_algorithms: should have 1");

    std::cout << "PASS: test_deprecated_functions" << std::endl;
}

int main() {
    test_basic_register_create();
    test_list_and_size();
    test_unregister();
    test_deprecated_functions();
    std::cout << "All algorithm registry tests passed!" << std::endl;
    return 0;
}
