#include "AStarDiagnostics.hpp"
#ifndef ASTAR_DISABLE_DIAGNOSTICS
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

void AStarDiagnostics::write() const {
    namespace fs = std::filesystem;
    fs::create_directories("logs/diag");

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

    std::string path = std::string("logs/auto/astar_") + ts + "_" + rand_str + ".log";
    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# AStar Exit Diagnostics\n"
        << "timestamp=" << ts << "_" << rand_str << "\n"
        << "status=" << (status ? status : "") << "\n"
        << "solution_cost=" << solution_cost << "\n"
        << "wall_ms=" << wall_ms << "\n"
        << "explored_count=" << explored_count << "\n"
        << "best_g_entries=" << best_g_size << "\n"
        << "step_pool_used=" << step_pool_used << "\n"
        << "step_pool_capacity=" << step_pool_capacity << "\n"
        << "items_pool=" << items_pool_size << "\n"
        << "items_pool_capacity=" << items_pool_capacity << "\n"
        << "open_set_pending=" << open_set_pending << "\n"
        << "pruned_by_cost=" << pruned_by_cost << "\n"
        << "pruned_by_best_g=" << pruned_by_best_g << "\n"
        << "pruned_by_f=" << pruned_by_f << "\n"
        << "pruned_by_caps=" << pruned_by_caps << "\n"
        << "steps_forged=" << steps_forged << "\n"
        << "estimated_peak_bytes=" << estimated_peak_bytes << "\n";
}

#endif // ASTAR_DISABLE_DIAGNOSTICS
