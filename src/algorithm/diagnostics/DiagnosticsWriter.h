#pragma once
#include <span>
#include <string>
#include <string_view>
#include <variant>

/// Free-function diagnostics writers, separated from the pure-data
/// diagnostics structs.
///
/// Compiled out when BESQ_DISABLE_DIAGNOSTICS is defined.
namespace DiagnosticsWriter {

/// A single key-value entry for the generic diagnostics writer.
struct Entry {
    const char* key{nullptr};               // points to static string literal
    using Value = std::variant<int64_t, std::string>;
    Value value{};

    Entry() = default;
    Entry(const char* k, int64_t v) noexcept : key(k), value(v) {}
    Entry(const char* k, std::string v) noexcept : key(k), value(std::move(v)) {}
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
