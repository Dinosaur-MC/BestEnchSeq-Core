#include "DiagnosticsWriter.h"
#ifndef BESQ_DISABLE_DIAGNOSTICS

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

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

// ─── AlgorithmDiagnostics writer ─────────────────────────────────────────────

void DiagnosticsWriter::write(const AlgorithmDiagnostics& diag) {
    if (diag.wall_ms < 10) return;  // skip trivial runs

    ensure_diag_dir();
    auto info = make_log_info();

    std::string path = std::string("logs/diag/")
                     + (diag.label[0] ? diag.label : "algo") + "_"
                     + info.ts + "_" + info.rand_part + ".log";
    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# " << (diag.label[0] ? diag.label : "Algorithm") << " Diagnostics\n"
        << "timestamp=" << info.ts << "_" << info.rand_part << "\n"
        << "status=" << (diag.status ? diag.status : "") << "\n"
        << "solution_cost=" << diag.solution_cost << "\n"
        << "wall_ms=" << diag.wall_ms << "\n"
        << "nodes_visited=" << diag.nodes_visited << "\n"
        << "nodes_pruned=" << diag.nodes_pruned << "\n"
        << "steps_forged=" << diag.steps_forged << "\n";
}

// ─── AStarDiagnostics writer ─────────────────────────────────────────────────

void DiagnosticsWriter::write(const AStarDiagnostics& diag) {
    ensure_diag_dir();
    auto info = make_log_info();

    std::string path = std::string("logs/diag/astar_")
                     + info.ts + "_" + info.rand_part + ".log";
    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# AStar Exit Diagnostics\n"
        << "timestamp=" << info.ts << "_" << info.rand_part << "\n"
        << "status=" << (diag.status ? diag.status : "") << "\n"
        << "solution_cost=" << diag.solution_cost << "\n"
        << "wall_ms=" << diag.wall_ms << "\n"
        << "explored_count=" << diag.explored_count << "\n"
        << "best_g_entries=" << diag.best_g_size << "\n"
        << "step_pool_used=" << diag.step_pool_used << "\n"
        << "step_pool_capacity=" << diag.step_pool_capacity << "\n"
        << "items_pool=" << diag.items_pool_size << "\n"
        << "items_pool_capacity=" << diag.items_pool_capacity << "\n"
        << "open_set_pending=" << diag.open_set_pending << "\n"
        << "pruned_by_cost=" << diag.pruned_by_cost << "\n"
        << "pruned_by_best_g=" << diag.pruned_by_best_g << "\n"
        << "pruned_by_f=" << diag.pruned_by_f << "\n"
        << "pruned_by_caps=" << diag.pruned_by_caps << "\n"
        << "steps_forged=" << diag.steps_forged << "\n"
        << "estimated_peak_bytes=" << diag.estimated_peak_bytes << "\n";
}

#endif // BESQ_DISABLE_DIAGNOSTICS
