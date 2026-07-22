#include "DiagnosticsWriter.h"
#ifndef BESQ_DISABLE_DIAGNOSTICS

#include <algorithm>
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

/// Maximum number of diagnostic log files to retain.  Oldest files beyond
/// this limit are pruned after each new write.
inline constexpr size_t MAX_DIAG_FILES = 128;

/// Remove the oldest diagnostic log files when the count exceeds MAX_DIAG_FILES.
void trim_diag_dir() {
    namespace fs = std::filesystem;

    fs::path dir("logs/diag");
    std::error_code ec;
    auto it = fs::directory_iterator(dir, ec);
    if (ec) return;  // directory doesn't exist yet — nothing to trim

    // Collect all .log files with their last-write time.
    struct Entry { fs::path path; fs::file_time_type mtime; };
    std::vector<Entry> files;
    for (const auto& entry : it) {
        if (entry.is_regular_file() && entry.path().extension() == ".log")
            files.push_back({entry.path(), entry.last_write_time(ec)});
    }

    // Evict oldest (last-write-time ascending → delete front).
    if (files.size() <= MAX_DIAG_FILES) return;

    std::partial_sort(files.begin(), files.begin() + (files.size() - MAX_DIAG_FILES),
                      files.end(),
                      [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });

    size_t to_remove = files.size() - MAX_DIAG_FILES;
    for (size_t i = 0; i < to_remove; ++i)
        fs::remove(files[i].path, ec);
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
        ofs << e.key << "=";
        if (auto* i = std::get_if<int64_t>(&e.value))
            ofs << *i;
        else
            ofs << std::get<std::string>(e.value);
        ofs << "\n";
    }

    // Trim directory to MAX_DIAG_FILES so log files don't accumulate unboundedly.
    trim_diag_dir();
}

#endif // BESQ_DISABLE_DIAGNOSTICS
