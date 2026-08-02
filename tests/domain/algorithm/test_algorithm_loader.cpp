#include "framework/test_utils.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/plugin/PluginAudit.h"
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

    expect(!loader.contains("astar"), "contains: astar is not built-in (moved to plugin)");
    expect(!loader.contains("dfs"), "contains: dfs is not built-in (moved to plugin)");
    expect(loader.contains("hamming"), "contains: hamming is available");
    expect(!loader.contains("nonexistent_algo"), "contains: unknown algorithm absent");

    std::cout << "PASS: test_loader_contains" << std::endl;
}

void test_loader_create() {
    AlgorithmLoader loader;
    loader.load_builtin();

    {
        auto algo = loader.create("astar");
        expect(algo == nullptr, "create: astar absent (moved to plugin)");
    }
    {
        auto algo = loader.create("dfs");
        expect(algo == nullptr, "create: dfs absent (moved to plugin)");
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
    expect(!loader.contains("astar"), "double load: astar absent (moved to plugin)");
    expect(!loader.contains("dfs"), "double load: dfs absent (moved to plugin)");
    expect(loader.contains("hamming"), "double load: hamming still present");

    std::cout << "PASS: test_loader_double_load" << std::endl;
}

// ─── Audit tests ───────────────────────────────────────────────────

void test_audit_default_state() {
    AlgorithmLoader loader;
    loader.load_builtin();

    // Before any plugin load, last_audit() should be nullptr
    expect(loader.last_audit() == nullptr, "audit: last_audit is null before any load");

    std::cout << "PASS: test_audit_default_state" << std::endl;
}

void test_audit_static_method() {
    // Scan a non-existent file — should return passed=false
    auto report = audit_plugin_binary("nonexistent_plugin.so");
    expect(!report.passed, "audit: nonexistent file returns passed=false");

    // Scan a regular source file (not a valid ELF/PE) — should return passed=false
    // because the binary format will not be recognized.
    auto report2 = audit_plugin_binary("test_algorithm_loader.cpp");
    expect(!report2.passed, "audit: non-binary file returns passed=false");

    std::cout << "PASS: test_audit_static_method" << std::endl;
}

void test_audit_report_defaults() {
    PluginAuditReport r;

    expect(r.passed, "audit: default report has passed=true");
    expect(!r.has_wx_segment, "audit: default report has no W+X");
    expect(r.extra_exports.empty(), "audit: default report has no extra exports");
    expect(r.dangerous_imports.empty(), "audit: default report has no dangerous imports");
    expect(r.linked_libraries.empty(), "audit: default report has no linked libs");
    expect(!r.has_manifest, "audit: default report has no manifest");
    expect(r.capability == PluginCapability::Unrestricted,
           "audit: default capability is Unrestricted");

    std::cout << "PASS: test_audit_report_defaults" << std::endl;
}

int main() {
    try {
        test_loader_builtin_list();
        test_loader_contains();
        test_loader_create();
        test_loader_size();
        test_loader_double_load();
        test_audit_default_state();
        test_audit_static_method();
        test_audit_report_defaults();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
