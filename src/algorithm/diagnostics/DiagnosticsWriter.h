#pragma once
#include <span>
#include <string>
#include <string_view>

/// Free-function diagnostics writers, separated from the pure-data
/// diagnostics structs.
///
/// Compiled out when BESQ_DISABLE_DIAGNOSTICS is defined.
namespace DiagnosticsWriter {

/// A single key-value entry for the generic diagnostics writer.
struct Entry {
    std::string key;
    std::string value;
};

#ifndef BESQ_DISABLE_DIAGNOSTICS

/// Generic key-value diagnostics writer.
///
/// Writes to logs/diag/<algorithm_name>_<timestamp>_<random>.log with format:
///
///   # <AlgorithmName> Exit Diagnostics
///   algorithm=<name>
///   status=<status>
///   wall_ms=<ms>
///   <key=value pairs from entries>
void write(std::string_view algorithm_name,
           std::span<const Entry> entries,
           int64_t wall_ms,
           std::string_view status);
#else
inline void write(std::string_view, std::span<const Entry>, int64_t, std::string_view) {}
#endif

} // namespace DiagnosticsWriter
