#pragma once

/// @file sandbox/SandboxedAlgorithm.h
/// Parent-side IAlgorithm proxy that runs the real plugin in a sandboxed
/// besq-worker subprocess, communicating over a socketpair.
///
/// The worker links the SAME algorithm kernel (besq-algo-core) and the
/// plugin is bare, so the kernel symbols unify — DiagnosticsService and
/// vtables live in the worker, one copy per process.
///
/// execute() sends AlgorithmInput to the worker, which runs the search with
/// a fully local ExecutionContext; progress / solution events are streamed
/// back and re-injected into the parent's ExecutionContext.

#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/sandbox/IpcProtocol.h"
#include "domain/algorithm/plugin/PluginAPI.h"

#include <string>
#include <vector>

namespace algorithm {

class SandboxedAlgorithm : public IAlgorithm {
  public:
    /// `plugin_path` — .so/.dll to load in the worker.
    /// `worker_path` — path to the besq-worker executable ("" → $BESQ_WORKER_PATH
    ///                 or "besq-worker" on PATH).
    SandboxedAlgorithm(std::string plugin_path, std::string worker_path,
                       PluginCapability capability);
    ~SandboxedAlgorithm() override;

    SandboxedAlgorithm(const SandboxedAlgorithm &)            = delete;
    SandboxedAlgorithm &operator=(const SandboxedAlgorithm &) = delete;

    // ── IAlgorithm ────────────────────────────────────────────────────
    std::string_view name() const noexcept override { return _name; }
    std::string_view version() const noexcept override { return _version; }
    AlgorithmMode supported_mode() const noexcept override { return _mode; }
    bool is_resumable() const noexcept override { return false; }
    double evaluate(int16_t ench_count) const noexcept override;

    void execute(const AlgorithmInput &input, ExecutionContext &ctx) override;
    bool simulate(const AlgorithmInput &input) const noexcept override;

    /// v1: not proxied — returns a default vanilla forge engine (the worker
    /// computes results itself; this only serves default process() replay).
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override;

    /// v1: sandboxed plugins are not resumable.
    IAlgorithmSerializer *get_serializer() noexcept override { return nullptr; }
    const IAlgorithmSerializer *get_serializer() const noexcept override { return nullptr; }

    /// Drain whatever the worker has written to its stderr so far (e.g. the
    /// malicious-test plugin's "OPEN BLOCKED" report, or worker diagnostics).
    /// Non-blocking: returns "" if nothing is buffered.
    std::string take_worker_stderr();

  private:
    void spawn_worker();
    void query_metadata();
    void send(ipc::MsgType type, const std::vector<uint8_t> &payload) const;
    bool recv(ipc::MsgType &type, std::vector<uint8_t> &payload) const;

    std::string _plugin_path;
    std::string _worker_path;
    // Capability → seccomp profile mapping is M3; retained for the field.
    [[maybe_unused]] PluginCapability _capability;
    int _fd = -1;
    [[maybe_unused]] int _stderr_fd = -1;  // worker's stderr (Linux; used by take_worker_stderr)
    [[maybe_unused]] long _pid = -1;  // used in the Linux destructor

    std::string _name;
    std::string _version;
    AlgorithmMode _mode = AlgorithmMode::direct;
};

} // namespace algorithm
