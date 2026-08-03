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
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include "domain/algorithm/plugin/PluginAPI.h"

#include <memory>
#include <span>
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

    /// Proxy serializer: checkpoint wrapping stays in the parent, the
    /// algorithm-state sections cross IPC to the worker's real serializer.
    IAlgorithmSerializer *get_serializer() noexcept override { return _serializer.get(); }
    const IAlgorithmSerializer *get_serializer() const noexcept override { return _serializer.get(); }

    /// Drain whatever the worker has written to its stderr so far (e.g. the
    /// malicious-test plugin's "OPEN BLOCKED" report, or worker diagnostics).
    /// Non-blocking: returns "" if nothing is buffered.
    std::string take_worker_stderr();

  private:
    /// Parent-side serializer proxy.  Keeps IAlgorithmSerializer::serialize /
    /// deserialize (checkpoint wrapping + input section) in the parent, but
    /// forwards ONLY the algorithm-state sections over IPC: the worker's real
    /// serializer produces/consumes them.  As a nested class it can use the
    /// enclosing SandboxedAlgorithm's private send/recv and pipe fds.  Large
    /// state (MB–GB) crosses in kChunkSize frames.
    class SandboxSerializer : public IAlgorithmSerializer {
      public:
        explicit SandboxSerializer(const SandboxedAlgorithm &sa) noexcept : _sa(sa) {}
        std::string_view algorithm_name() const noexcept override { return _sa._name; }
        std::string_view algorithm_version() const noexcept override { return _sa._version; }
      protected:
        std::vector<checkpoint::Section> _serialize_state(const IAlgorithm &) const override;
        bool _deserialize_state(IAlgorithm &,
                                std::span<const checkpoint::Section>) const override;
      private:
        /// Read frames until the MsgResult response, skipping straggler
        /// progress/solution events queued before the worker paused.
        std::vector<uint8_t> recv_result() const;
        const SandboxedAlgorithm &_sa;
    };

    void spawn_worker();
    void query_metadata();
    void send(ipc::MsgType type, const std::vector<uint8_t> &payload) const;
    bool recv(ipc::MsgType &type, std::vector<uint8_t> &payload) const;

    std::string _plugin_path;
    std::string _worker_path;
    // Capability → seccomp profile mapping is M3; retained for the field.
    [[maybe_unused]] PluginCapability _capability;
    // Linux: _fd is the full-duplex socketpair.  Windows: _fd reads the
    // child→parent pipe, _write_fd writes the parent→child pipe.
    int _fd = -1;
    int _write_fd = -1;
    [[maybe_unused]] int _stderr_fd = -1;  // worker's stderr (used by take_worker_stderr)
    [[maybe_unused]] long _pid = -1;  // Linux pid / Windows process id
    // Windows-only handles (Job Object + process + raw stderr pipe) for
    // cleanup / limits / stderr capture.
    [[maybe_unused]] void *_proc_handle = nullptr;
    [[maybe_unused]] void *_job_handle = nullptr;
    [[maybe_unused]] void *_stderr_handle = nullptr;

    std::string _name;
    std::string _version;
    AlgorithmMode _mode = AlgorithmMode::direct;
    std::unique_ptr<SandboxSerializer> _serializer;  // created after query_metadata

    /// Set by execute() while the loop is paused and has yielded the IPC pipe
    /// (blocked in wait_if_paused).  The SandboxSerializer waits on this so its
    /// MsgSerializeState exchange can't race the execute loop's read_frame.
    std::atomic<bool> _pipe_yielded{false};
};

} // namespace algorithm
