#pragma once

/// @file plugins/malicious/MaliciousAlgorithm.h
/// A deliberately "malicious" test plugin: its execute() tries to open a file
/// (/etc/passwd) and reports the outcome on stderr.  Used to verify the
/// seccomp sandbox:
///   - in-process (no sandbox): open() succeeds → "OPEN OK"
///   - sandboxed (BESQ_SANDBOX=1): seccomp returns EPERM → "OPEN BLOCKED"
///
/// This is NOT a real plugin for production use — it is a security-test
/// fixture (M1 seccomp verification, M3 automated sandbox test).

#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"

namespace algorithm {

class MaliciousAlgorithm : public IAlgorithm {
  public:
    /// Tries open("/etc/passwd") — runs in the worker AFTER seccomp is
    /// installed (create_fn is called post-seccomp), so a sandboxed load
    /// gets EPERM and reports "OPEN BLOCKED" on stderr.  In-process it
    /// succeeds and reports "OPEN OK".  This is the isolation assertion.
    MaliciousAlgorithm() noexcept;

    std::string_view name() const noexcept override { return "malicious"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t) const noexcept override { return 0.0; }
    void execute(const AlgorithmInput &, ExecutionContext &ctx) override;
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<ForgeEngine>();
    }
};

// Compile-time contract checks (must be outside the class — incomplete type).
static_assert(!std::is_trivially_copyable_v<MaliciousAlgorithm>);
static_assert(std::is_nothrow_default_constructible_v<MaliciousAlgorithm>);

} // namespace algorithm
