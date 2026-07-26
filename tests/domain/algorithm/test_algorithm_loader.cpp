#include "framework/test_utils.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/IAlgorithm.h"

using namespace algorithm;

// ─── Tests ─────────────────────────────────────────────────────────

void test_loader_builtin_list() {
    AlgorithmLoader loader;
    loader.load_builtin();

    auto names = loader.list();
    expect(names.size() >= 3, "built-in list contains at least 3 algorithms");

    std::cout << "PASS: test_loader_builtin_list" << std::endl;
}

void test_loader_contains() {
    AlgorithmLoader loader;
    loader.load_builtin();

    expect(loader.contains("astar"), "contains: astar is available");
    expect(loader.contains("dfs"), "contains: dfs is available");
    expect(loader.contains("hamming"), "contains: hamming is available");
    expect(!loader.contains("nonexistent_algo"), "contains: unknown algorithm absent");

    std::cout << "PASS: test_loader_contains" << std::endl;
}

void test_loader_create() {
    AlgorithmLoader loader;
    loader.load_builtin();

    {
        auto algo = loader.create("astar");
        expect(algo != nullptr, "create: astar instance not null");
        expect(std::string(algo->name()) == "astar", "create: astar name matches");
    }
    {
        auto algo = loader.create("dfs");
        expect(algo != nullptr, "create: dfs instance not null");
        expect(std::string(algo->name()) == "dfs", "create: dfs name matches");
    }
    {
        auto algo = loader.create("hamming");
        expect(algo != nullptr, "create: hamming instance not null");
        expect(std::string(algo->name()) == "hamming", "create: hamming name matches");
    }
    {
        auto algo = loader.create("unknown_algo");
        expect(algo == nullptr, "create: unknown algorithm returns null");
    }

    std::cout << "PASS: test_loader_create" << std::endl;
}

void test_loader_size() {
    AlgorithmLoader loader;
    loader.load_builtin();

    auto names = loader.list();
    expect(loader.size() == names.size(), "size() equals list().size()");

    std::cout << "PASS: test_loader_size" << std::endl;
}

void test_loader_double_load() {
    AlgorithmLoader loader;
    loader.load_builtin();

    auto first_list = loader.list();
    auto first_size = loader.size();

    // Second call should be a no-op
    loader.load_builtin();

    auto second_list = loader.list();
    auto second_size = loader.size();

    expect(second_size == first_size, "double load: size unchanged");
    expect(second_list.size() == first_list.size(), "double load: list size unchanged");
    expect(loader.contains("astar"), "double load: astar still present");
    expect(loader.contains("dfs"), "double load: dfs still present");
    expect(loader.contains("hamming"), "double load: hamming still present");

    std::cout << "PASS: test_loader_double_load" << std::endl;
}

int main() {
    try {
        test_loader_builtin_list();
        test_loader_contains();
        test_loader_create();
        test_loader_size();
        test_loader_double_load();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
