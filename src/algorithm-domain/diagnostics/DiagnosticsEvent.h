#pragma once
#include "../types/AlgorithmTypes.h"
#include "AlgorithmDiagnostics.h"
#include "DiagnosticsWriter.h"
#include <chrono>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace algorithm {

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
};

struct ProgressPayload {
    uint8_t pct{}; // 0–100
    ProgressStatus status{};
};

struct SolutionPayload {
    std::shared_ptr<const EnchSolution> solution{};
};

struct StatePayload {
    AlgorithmState prev{};
    AlgorithmState curr{};
};

using PayloadVariant = std::variant<ExitPayload, ProgressPayload, SolutionPayload, StatePayload>;

} // namespace detail

// ─── DiagnosticsEvent ─────────────────────────────────────────────

struct DiagnosticsEvent {
    using ExitPayload     = detail::ExitPayload;
    using ProgressPayload = detail::ProgressPayload;
    using SolutionPayload = detail::SolutionPayload;
    using StatePayload    = detail::StatePayload;
    using PayloadVariant  = detail::PayloadVariant;

    DiagEventKind kind{};
    std::string algorithm_name{}; // owns its name string
    size_t task_id{0};
    std::chrono::system_clock::time_point timestamp{};

    DiagnosticsEvent() = default;

    /// Constructor for emplace support (takes kind + name + task_id + any payload type).
    /// Use abbreviated function template (C++20) to enable forwarding.
    DiagnosticsEvent(DiagEventKind k, std::string name, size_t tid, auto &&p)
        : kind(k), algorithm_name(std::move(name)), task_id(tid), timestamp(std::chrono::system_clock::now()),
          payload(std::forward<decltype(p)>(p)) {}

    detail::PayloadVariant payload;
};

} // namespace algorithm
