#include "AlgorithmDiagnostics.h"
#ifndef BESQ_DISABLE_DIAGNOSTICS
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

void AlgorithmDiagnostics::write() const {
    // Skip file write for trivial runs (wall < 10ms) — avoids ~1ms I/O
    // overhead for fast algorithms where diagnostics add no value.
    if (wall_ms < 10) return;

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

    std::string path = std::string("logs/diag/") + (label[0] ? label : "algo") + "_"
                     + ts + "_" + rand_str + ".log";
    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# " << (label[0] ? label : "Algorithm") << " Diagnostics\n"
        << "timestamp=" << ts << "_" << rand_str << "\n"
        << "status=" << (status ? status : "") << "\n"
        << "solution_cost=" << solution_cost << "\n"
        << "wall_ms=" << wall_ms << "\n"
        << "nodes_visited=" << nodes_visited << "\n"
        << "nodes_pruned=" << nodes_pruned << "\n"
        << "steps_forged=" << steps_forged << "\n";
}

#endif // BESQ_DISABLE_DIAGNOSTICS
