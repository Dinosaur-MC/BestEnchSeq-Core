#pragma once
#include "algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "algorithm/diagnostics/DiagnosticsWriter.h"
#include "types/AlgorithmTypes.h"
#include <chrono>
#include <memory>
#include <variant>
#include <vector>

enum class DiagEventKind : uint8_t {
    Exit,
    Progress,
    Solution,
    StateChange,
};

// ─── Payload types (defined at namespace scope to avoid MSVC STL
//     variant + Clang nested-struct default-ctor bug) ──────────────

namespace detail {

struct ExitPayload {
    std::unique_ptr<AlgorithmDiagnostics> diagnostics{};
    AlgorithmOutput output{};
    std::string status{};
    int64_t wall_ms{};
    DiagnosticsWriter::Entry nodes_visited{};
    DiagnosticsWriter::Entry nodes_pruned{};
    DiagnosticsWriter::Entry steps_forged{};
    std::vector<DiagnosticsWriter::Entry> flush_entries{};
};

struct ProgressPayload {
    double percent{};
    ProgressStatus status{};
};

struct SolutionPayload {
    std::shared_ptr<const compact::EnchSolution> solution{};
};

struct StatePayload {
    AlgorithmState prev{};
    AlgorithmState curr{};
};

using PayloadVariant = std::variant<
    ExitPayload,
    ProgressPayload,
    SolutionPayload,
    StatePayload
>;

} // namespace detail

// ─── DiagnosticsEvent ─────────────────────────────────────────────

struct DiagnosticsEvent {
    using ExitPayload      = detail::ExitPayload;
    using ProgressPayload  = detail::ProgressPayload;
    using SolutionPayload  = detail::SolutionPayload;
    using StatePayload     = detail::StatePayload;
    using PayloadVariant   = detail::PayloadVariant;

    DiagEventKind kind{};
    const char* algorithm_name{nullptr};        // points to static string literal
    std::chrono::system_clock::time_point timestamp{};

    DiagnosticsEvent() = default;

    /// Constructor for emplace support (takes kind + name + any payload type).
    /// Use abbreviated function template (C++20) to enable forwarding.
    DiagnosticsEvent(DiagEventKind k, const char* name, auto&& p)
        : kind(k), algorithm_name(name),
          timestamp(std::chrono::system_clock::now()),
          payload(std::forward<decltype(p)>(p)) {}

    detail::PayloadVariant payload;
};
