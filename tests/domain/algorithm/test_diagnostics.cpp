#include "framework/test_utils.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/diagnostics/DiagnosticsWriter.h"
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

using namespace algorithm;

// ─── AlgorithmDiagnostics::flush ─────────────────────────────────────────
//
// Verify that the base diagnostics flush writes "status" and
// "solution_cost" entries.

void test_algorithm_diagnostics_flush() {
    AlgorithmDiagnostics diag;
    diag.algorithm_name = "test_algo";
    diag.status = "Complete";
    diag.solution_cost = 42;

    std::vector<DiagnosticsWriter::Entry> entries;
    diag.flush(entries);

    expect(entries.size() == 4,
           "AlgorithmDiagnostics flush should produce 4 entries (status, solution_cost, diag_schema_version, normalized_explored_states)");

    // status is the first entry (base class)
    expect(std::string(entries[0].key) == "status",
           "first key should be 'status'");
    expect(std::holds_alternative<std::string>(entries[0].value),
           "status value should hold a string");
    expect(std::get<std::string>(entries[0].value) == "Complete",
           "status value should be 'Complete'");

    // solution_cost is the second entry
    expect(std::string(entries[1].key) == "solution_cost",
           "second key should be 'solution_cost'");
    expect(std::holds_alternative<int64_t>(entries[1].value),
           "solution_cost value should hold int64_t");
    expect(std::get<int64_t>(entries[1].value) == 42,
           "solution_cost value should be 42");

    // Common-core fields (spec §4): schema version + normalized explored states
    expect(std::string(entries[2].key) == "diag_schema_version",
           "third key should be 'diag_schema_version'");
    expect(std::get<int64_t>(entries[2].value) == 1,
           "diag_schema_version should default to 1");
    expect(std::string(entries[3].key) == "normalized_explored_states",
           "fourth key should be 'normalized_explored_states'");
    expect(std::get<int64_t>(entries[3].value) == -1,
           "normalized_explored_states should default to -1");

    std::cout << "PASS: test_algorithm_diagnostics_flush" << std::endl;
}

// ─── SearchDiagnostics::flush ────────────────────────────────────────────
//
// Verify that SearchDiagnostics flush adds initial_bound, final_bound,
// solutions_found, and max_depth on top of the base entries.

void test_search_diagnostics_flush() {
    SearchDiagnostics diag;
    diag.status = "Cancelled";
    diag.solution_cost = 10;
    diag.initial_bound = 100;
    diag.final_bound = 50;
    diag.solutions_found = 3;
    diag.max_depth_reached = 12;

    std::vector<DiagnosticsWriter::Entry> entries;
    diag.flush(entries);

    // base (4) + search (4) = 8
    expect(entries.size() == 8,
           "SearchDiagnostics flush should produce 8 entries");

    // Verify search-specific entry keys by name (order: parent then child)
    bool found_initial_bound = false;
    bool found_final_bound = false;
    bool found_solutions_found = false;
    bool found_max_depth = false;

    for (const auto& e : entries) {
        std::string key(e.key ? e.key : "");
        if (key == "initial_bound") {
            found_initial_bound = true;
            expect(std::get<int64_t>(e.value) == 100,
                   "initial_bound should be 100");
        } else if (key == "final_bound") {
            found_final_bound = true;
            expect(std::get<int64_t>(e.value) == 50,
                   "final_bound should be 50");
        } else if (key == "solutions_found") {
            found_solutions_found = true;
            expect(std::get<int64_t>(e.value) == 3,
                   "solutions_found should be 3");
        } else if (key == "max_depth") {
            found_max_depth = true;
            expect(std::get<int64_t>(e.value) == 12,
                   "max_depth should be 12");
        }
    }

    expect(found_initial_bound, "SearchDiagnostics flush should include initial_bound");
    expect(found_final_bound,   "SearchDiagnostics flush should include final_bound");
    expect(found_solutions_found, "SearchDiagnostics flush should include solutions_found");
    expect(found_max_depth,     "SearchDiagnostics flush should include max_depth");

    std::cout << "PASS: test_search_diagnostics_flush" << std::endl;
}

// ─── PoolSearchDiagnostics::flush ────────────────────────────────────────
//
// Verify that PoolSearchDiagnostics flush adds pool capacity entries on
// top of Search + base entries.

void test_pool_search_diagnostics_flush() {
    PoolSearchDiagnostics diag;
    diag.status = "Complete";
    diag.solution_cost = 5;
    diag.initial_bound = 200;
    diag.final_bound = 0;
    diag.solutions_found = 1;
    diag.max_depth_reached = 8;
    diag.items_pool_used = 64;
    diag.items_pool_capacity = 128;
    diag.step_pool_used = 32;
    diag.step_pool_capacity = 64;

    std::vector<DiagnosticsWriter::Entry> entries;
    diag.flush(entries);

    // base (4) + search (4) + pool (4) = 12
    expect(entries.size() == 12,
           "PoolSearchDiagnostics flush should produce 12 entries");

    // Verify pool-specific entry keys and values
    bool found_items_pool_used = false;
    bool found_items_pool_capacity = false;
    bool found_step_pool_used = false;
    bool found_step_pool_capacity = false;

    for (const auto& e : entries) {
        std::string key(e.key ? e.key : "");
        if (key == "items_pool_used") {
            found_items_pool_used = true;
            expect(std::get<int64_t>(e.value) == 64,
                   "items_pool_used should be 64");
        } else if (key == "items_pool_capacity") {
            found_items_pool_capacity = true;
            expect(std::get<int64_t>(e.value) == 128,
                   "items_pool_capacity should be 128");
        } else if (key == "step_pool_used") {
            found_step_pool_used = true;
            expect(std::get<int64_t>(e.value) == 32,
                   "step_pool_used should be 32");
        } else if (key == "step_pool_capacity") {
            found_step_pool_capacity = true;
            expect(std::get<int64_t>(e.value) == 64,
                   "step_pool_capacity should be 64");
        }
    }

    expect(found_items_pool_used,     "Pool flush should include items_pool_used");
    expect(found_items_pool_capacity, "Pool flush should include items_pool_capacity");
    expect(found_step_pool_used,      "Pool flush should include step_pool_used");
    expect(found_step_pool_capacity,  "Pool flush should include step_pool_capacity");

    std::cout << "PASS: test_pool_search_diagnostics_flush" << std::endl;
}

// ─── DiagnosticsWriter::write (smoke test) ───────────────────────────────
//
// Verify the write function compiles and can be called.
// The real implementation writes to files (logs/diag/*), so we skip that
// path and do a lightweight smoke call instead.

void test_diagnostics_writer_skip_short() {
    // Call write with wall_ms < 10 — the real implementation returns early
    // in this case. Under BESQ_DISABLE_DIAGNOSTICS this is a no-op anyway.
    std::vector<DiagnosticsWriter::Entry> entries;
    entries.emplace_back("test_key", int64_t(1));

    DiagnosticsWriter::write("test", entries, 5, "ok");

    // If we reach here the function exists, links, and doesn't crash
    expect(true, "DiagnosticsWriter::write should be callable without error");

    std::cout << "PASS: test_diagnostics_writer_skip_short" << std::endl;
}

// ─── DiagnosticsWriter::Entry ────────────────────────────────────────────
//
// Verify Entry construction and variant access.

void test_diagnostics_writer_entry() {
    // Default construction
    DiagnosticsWriter::Entry default_entry;
    expect(default_entry.key == nullptr,
           "default entry key should be nullptr");
    expect(std::holds_alternative<int64_t>(default_entry.value),
           "default entry value should hold int64_t");
    expect(std::get<int64_t>(default_entry.value) == 0,
           "default entry value should be 0");

    // int64_t construction
    DiagnosticsWriter::Entry int_entry("count", int64_t(99));
    expect(std::string(int_entry.key) == "count",
           "int entry key should be 'count'");
    expect(std::holds_alternative<int64_t>(int_entry.value),
           "int entry value should hold int64_t");
    expect(std::get<int64_t>(int_entry.value) == 99,
           "int entry value should be 99");

    // string construction
    DiagnosticsWriter::Entry str_entry("label", std::string("test_value"));
    expect(std::string(str_entry.key) == "label",
           "string entry key should be 'label'");
    expect(std::holds_alternative<std::string>(str_entry.value),
           "string entry value should hold string");
    expect(std::get<std::string>(str_entry.value) == "test_value",
           "string entry value should be 'test_value'");

    // Verify variant index matches expected type
    // int64_t is index 0 (first alternative), string is index 1
    expect(str_entry.value.index() == 1,
           "string variant should have index 1");
    expect(int_entry.value.index() == 0,
           "int64_t variant should have index 0");

    std::cout << "PASS: test_diagnostics_writer_entry" << std::endl;
}

// ─── PartitionDpDiagnostics::flush ───────────────────────────────────────
//
// Template for Catalan/partition DP algorithms (bb_dp / dp_merge / DP
// plugins) — spec §8.  base (4) + search (4) + dp (9) = 17 entries, with the
// `dp_` prefix per the naming convention (§6).

void test_partition_dp_diagnostics_flush() {
    PartitionDpDiagnostics diag;
    diag.status               = "Complete";
    diag.solution_cost        = 150;
    diag.initial_bound        = 155;
    diag.final_bound          = 150;
    diag.dp_subproblems_solved = 131072;
    diag.dp_cache_slots       = 131072;
    diag.dp_cache_hits        = 0;
    diag.dp_max_frontier_size = 2;
    diag.dp_cap_pruned        = 12345;
    diag.dp_bound_pruned      = 67890;
    diag.dp_pareto_dropped    = 11111;
    diag.dp_ub_cost           = 155;
    diag.dp_pass_b_ran        = false;
    diag.normalized_explored_states = 131072;

    std::vector<DiagnosticsWriter::Entry> entries;
    diag.flush(entries);

    expect(entries.size() == 17,
           "PartitionDpDiagnostics flush should produce 17 entries");

    bool found_solved = false, found_slots = false, found_hits = false,
         found_max_f = false, found_cap = false, found_bound = false,
         found_pareto = false, found_ub = false, found_pass_b = false;
    for (const auto& e : entries) {
        std::string key(e.key ? e.key : "");
        if (key == "dp_subproblems_solved") found_solved = true;
        if (key == "dp_cache_slots")        found_slots = true;
        if (key == "dp_cache_hits")         found_hits = true;
        if (key == "dp_max_frontier_size")  found_max_f = true;
        if (key == "dp_cap_pruned")         found_cap = true;
        if (key == "dp_bound_pruned")       found_bound = true;
        if (key == "dp_pareto_dropped")     found_pareto = true;
        if (key == "dp_ub_cost")            found_ub = true;
        if (key == "dp_pass_b_ran")         found_pass_b = true;
    }
    expect(found_solved && found_slots && found_hits && found_max_f &&
           found_cap && found_bound && found_pareto && found_ub && found_pass_b,
           "all dp_* keys should be flushed");
    std::cout << "PASS: test_partition_dp_diagnostics_flush" << std::endl;
}

int main() {
    try {
        test_algorithm_diagnostics_flush();
        test_search_diagnostics_flush();
        test_pool_search_diagnostics_flush();
        test_partition_dp_diagnostics_flush();
        test_diagnostics_writer_skip_short();
        test_diagnostics_writer_entry();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
