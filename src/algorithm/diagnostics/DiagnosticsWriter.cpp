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

#endif // BESQ_DISABLE_DIAGNOSTICS
