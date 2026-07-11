#include "framework/test_utils.h"
#include "registries/AlgorithmRegistry.h"
#include "registries/RegistryAccess.h"

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
    auto& reg = registries::algorithms();
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
    auto& reg = registries::algorithms();
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
    auto& reg = registries::algorithms();
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

void test_unregister_preserves_others() {
    auto& reg = registries::algorithms();
    reg.clear();

    reg.register_algorithm("keep_a", []{ return std::make_unique<TestAlgorithm>(); });
    reg.register_algorithm("remove_b", []{ return std::make_unique<TestAlgorithm>(); });
    reg.register_algorithm("keep_c", []{ return std::make_unique<TestAlgorithm>(); });

    expect(reg.size() == 3, "unregister_multi: size 3 before");
    expect(reg.contains("keep_a"), "unregister_multi: keep_a present");
    expect(reg.contains("remove_b"), "unregister_multi: remove_b present");
    expect(reg.contains("keep_c"), "unregister_multi: keep_c present");

    reg.unregister_algorithm("remove_b");

    expect(reg.size() == 2, "unregister_multi: size 2 after");
    expect(reg.contains("keep_a"), "unregister_multi: keep_a still present");
    expect(!reg.contains("remove_b"), "unregister_multi: remove_b gone");
    expect(reg.contains("keep_c"), "unregister_multi: keep_c still present");

    std::cout << "PASS: test_unregister_preserves_others" << std::endl;
}

void test_contains_and_list() {
    auto& reg = registries::algorithms();
    reg.clear();

    reg.register_algorithm("legacy", []{ return std::make_unique<TestAlgorithm>(); });

    expect(reg.contains("legacy"), "contains: legacy should work");
    expect(!reg.contains("ghost"), "contains: ghost not found");

    auto items = reg.list();
    expect(items.size() == 1, "list: should have 1 entry");

    std::cout << "PASS: test_contains_and_list" << std::endl;
}

void test_duplicate_registration() {
    auto& reg = registries::algorithms();
    reg.clear();

    reg.register_algorithm("dup_test", []{ return std::make_unique<TestAlgorithm>(); });
    expect(reg.contains("dup_test"), "duplicate: first registration works");

    // Re-register same name should overwrite, not crash
    reg.register_algorithm("dup_test", []{ return std::make_unique<TestAlgorithm>(); });
    expect(reg.contains("dup_test"), "duplicate: still contains after second registration");

    auto algo = reg.create("dup_test");
    expect(algo != nullptr, "duplicate: create returns non-null after second registration");

    std::cout << "PASS: test_duplicate_registration" << std::endl;
}

int main() {
    try {
        test_basic_register_create();
        test_list_and_size();
        test_unregister();
        test_unregister_preserves_others();
        test_contains_and_list();
        test_duplicate_registration();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
