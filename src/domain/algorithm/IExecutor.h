#pragma once

/// @file algorithm/IExecutor.h
/// The algorithm domain's authoritative executor interface.
///
/// The whole algorithm domain is entered through the executor: it owns the
/// ExecutionContext, drives IAlgorithm::execute(), runs the state machine, and
/// (for resumable algorithms) produces/consumes checkpoints.  Callers
/// (orchestration) depend on THIS interface and get either an in-process
/// `AlgorithmExecutor` or a `SandboxedExecutor` that runs a real executor
/// inside a sandboxed worker process — the same contract, with the sandbox
/// seam moved ABOVE the executor instead of tearing
/// executor/context/algorithm apart and reconnecting them over IPC.

#include "domain/algorithm/serialization/Checkpoint.h"
#include "domain/algorithm/types/AlgorithmState.h"
#include "domain/algorithm/types/AlgorithmTypes.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace algorithm {

class IExecutor {
public:
    virtual ~IExecutor() noexcept = default;

    /// Inspect a checkpoint blob without constructing an executor: validates
    /// magic/version/CRC and returns the checkpoint's MetaHeader, whose
    /// algorithm_tag names the algorithm that wrote it.  The resume flow
    /// (CLI --resume / GUI restore) uses it to create the right executor,
    /// then calls start(blob) to restore the computation.
    /// Returns a MetaHeader with an EMPTY algorithm_tag when the blob is not
    /// a valid checkpoint — callers must check before use.
    static checkpoint::MetaHeader peek(std::span<const uint8_t> data);

    // ── Metadata / preflight ──────────────────────────────────────────
    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view version() const noexcept = 0;
    virtual AlgorithmMode supported_mode() const noexcept = 0;
    virtual bool simulate(const AlgorithmInput& input) const noexcept = 0;

    // ── Run lifecycle ─────────────────────────────────────────────────
    virtual void start(AlgorithmInput input) = 0;
    virtual void start(const std::vector<uint8_t>& checkpoint) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void cancel() = 0;
    virtual AlgorithmState wait() = 0;
    virtual AlgorithmState state() const noexcept = 0;
    virtual double progress() const noexcept = 0;

    // ── Output / serialization ────────────────────────────────────────
    virtual AlgorithmOutput output() const = 0;
    virtual std::vector<uint8_t> serialize_state() const = 0;
    virtual bool is_serializable() const noexcept = 0;
};

} // namespace algorithm
