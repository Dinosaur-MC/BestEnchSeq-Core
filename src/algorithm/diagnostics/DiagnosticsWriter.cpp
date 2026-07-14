#include "DiagnosticsWriter.h"
#ifndef BESQ_DISABLE_DIAGNOSTICS

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

// ─── Shared helpers ──────────────────────────────────────────────────────────

struct LogFileInfo {
    std::string ts;
    std::string rand_part;
};

LogFileInfo make_log_info() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_buf);

    std::random_device rd;
    char rand_str[9];
    for (int i = 0; i < 8; ++i)
        rand_str[i] = "0123456789abcdef"[rd() % 16];
    rand_str[8] = '\0';

    return {ts, rand_str};
}

void ensure_diag_dir() {
    std::filesystem::create_directories("logs/diag");
}

} // anonymous namespace

// ─── Generic KV writer ──────────────────────────────────────────────────────

void DiagnosticsWriter::write(std::string_view algorithm_name,
                              std::span<const Entry> entries,
                              int64_t wall_ms,
                              std::string_view status) {
    if (wall_ms < 10) return;  // skip trivial runs

    ensure_diag_dir();
    auto info = make_log_info();

    std::string path = std::string("logs/diag/")
                     + std::string(algorithm_name) + "_"
                     + info.ts + "_" + info.rand_part + ".log";
    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# " << algorithm_name << " Exit Diagnostics\n"
        << "algorithm=" << algorithm_name << "\n"
        << "status=" << status << "\n"
        << "wall_ms=" << wall_ms << "\n";
    for (const auto& e : entries) {
        ofs << e.key << "=" << e.value << "\n";
    }
}

// ─── AlgorithmDiagnostics writer (deprecated) ────────────────────────────────

void DiagnosticsWriter::write(const AlgorithmDiagnostics& diag) {
    // Convert to KV entries and delegate to the generic writer.
    // Note: nodes_visited/pruned/steps_forged now come from ctx atomics
    // via Executor::_finalize(), not from this struct.
    std::vector<Entry> entries;
    entries.reserve(1);
    entries.push_back({"solution_cost", std::to_string(diag.solution_cost)});

    write(diag.algorithm_name, entries, diag.wall_ms, diag.status);
}

// ─── AStarDiagnostics writer (deprecated) ────────────────────────────────────

void DiagnosticsWriter::write(const AStarDiagnostics& diag) {
    // Convert to KV entries and delegate to the generic writer.
    // Note: steps_forged and wall_ms now come from ctx atomics via
    // Executor::_finalize(), not from the diagnostics struct.
    std::vector<Entry> entries;
    entries.reserve(13);
    entries.push_back({"solution_cost", std::to_string(diag.solution_cost)});
    entries.push_back({"explored_count", std::to_string(diag.explored_count)});
    entries.push_back({"best_g_entries", std::to_string(diag.best_g_entries)});
    entries.push_back({"step_pool_used", std::to_string(diag.step_pool_used)});
    entries.push_back({"step_pool_capacity", std::to_string(diag.step_pool_capacity)});
    entries.push_back({"items_pool", std::to_string(diag.items_pool_used)});
    entries.push_back({"items_pool_capacity", std::to_string(diag.items_pool_capacity)});
    entries.push_back({"open_set_pending", std::to_string(diag.open_set_pending)});
    entries.push_back({"pruned_by_cost", std::to_string(diag.pruned_by_cost)});
    entries.push_back({"pruned_by_best_g", std::to_string(diag.pruned_by_best_g)});
    entries.push_back({"pruned_by_f", std::to_string(diag.pruned_by_f)});
    entries.push_back({"pruned_by_caps", std::to_string(diag.pruned_by_caps)});
    entries.push_back({"estimated_peak_bytes", std::to_string(diag.estimated_peak_bytes)});

    write("astar", entries, diag.wall_ms, diag.status);
}

#endif // BESQ_DISABLE_DIAGNOSTICS
