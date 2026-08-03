#pragma once

/// @file sandbox/SandboxedExecutor.h
/// Parent-side IExecutor that runs a REAL AlgorithmExecutor inside a sandboxed
/// besq-worker subprocess.
///
/// The sandbox seam lives ABOVE the executor: the worker hosts an
/// AlgorithmExecutor (owning its ExecutionContext, the plugin IAlgorithm and
/// its serializer) fully locally, and this class mirrors the executor's public
/// surface over coarse IPC messages — MsgRun/MsgResumeRun for lifecycle,
/// MsgPause/Resume/Cancel for control, MsgSerializeState for checkpoints, and
/// a streamed MsgResult carrying the final AlgorithmOutput.
///
/// Compare with the old design (SandboxedAlgorithm, an IAlgorithm proxy): it
/// tore executor/context/algorithm apart across the process boundary and
/// reconnected them with handshakes (_pipe_yielded, control notifier, a proxy
/// serializer, section codecs).  None of that exists here — everything the
/// executor already does stays inside the worker.
///
/// Threading contract (mirrors AlgorithmExecutor):
///   - start() spawns a single reader thread that is the ONLY reader of the
///     worker→parent channel for the duration of the run.  wait() joins it.
///   - pause()/resume()/cancel() are safe from any thread (small atomic frame
///     writes under the write mutex).
///   - serialize_state() is valid only while Paused; it enqueues an intent and
///     the reader thread performs the IPC exchange (it owns the pipe), so it
///     never races wait()'s reader.
///   - Metadata/preflight calls (simulate/evaluate) must happen BEFORE start()
///     (no reader active yet) — same as in-process preflight ordering.

#include "domain/algorithm/IExecutor.h"
#include "domain/algorithm/plugin/PluginAPI.h"
#include "domain/algorithm/sandbox/IpcProtocol.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace algorithm {

class SandboxedExecutor : public IExecutor {
public:
    /// `plugin_path` — .so/.dll to load in the worker.
    /// `worker_path` — path to the besq-worker executable ("" → $BESQ_WORKER_PATH
    ///                 or <exe_dir>/besq-worker[.exe], then PATH).
    SandboxedExecutor(std::string plugin_path, std::string worker_path, PluginCapability capability);
    ~SandboxedExecutor() override;

    SandboxedExecutor(const SandboxedExecutor&) = delete;
    SandboxedExecutor& operator=(const SandboxedExecutor&) = delete;

    // ── IExecutor ──────────────────────────────────────────────────────
    std::string_view name() const noexcept override { return _name; }
    std::string_view version() const noexcept override { return _version; }
    AlgorithmMode supported_mode() const noexcept override { return _mode; }
    bool simulate(const AlgorithmInput& input) const noexcept override;

    void start(AlgorithmInput input) override;
    void start(const std::vector<uint8_t>& checkpoint) override;
    void pause() override;
    void resume() override;
    void cancel() override;
    AlgorithmState wait() override;
    AlgorithmState state() const noexcept override { return _state.load(std::memory_order_acquire); }
    double progress() const noexcept override { return _progress.load(std::memory_order_acquire); }
    AlgorithmOutput output() const override;
    std::vector<uint8_t> serialize_state() const override;
    bool is_serializable() const noexcept override { return _serializable; }

    /// Windows binary-IPC smoke helper (round-trips a payload containing 0x1A).
    double evaluate(int16_t ench_count) const noexcept;

    /// Drain the worker's stderr so far (non-blocking).  Used by the sandbox
    /// test to assert the malicious plugin's seccomp report ("OPEN BLOCKED").
    std::string take_worker_stderr();

private:
    /// Per-run serializer handshake.  The caller's serialize_state() fills
    /// `pending`, wakes the reader thread, and waits; the reader (the single
    /// pipe owner) sends MsgSerializeState, receives the blob and signals.
    struct SerializeIntent {
        std::mutex mtx;
        std::condition_variable cv;
        bool pending = false;
        bool error = false;
        std::vector<uint8_t> result; // opaque checkpoint blob
    };

    void spawn_worker();
    void query_metadata();
    void send(ipc::MsgType type, const std::vector<uint8_t>& payload) const;
    bool recv(ipc::MsgType& type, std::vector<uint8_t>& payload) const;
    void begin_run();
    void reader_loop() noexcept;
    void wake_reader() const noexcept;
    /// Terminal-state transition from the reader thread: Cancelled wins (it is
    /// a terminal state set by cancel() and must not be clobbered).
    void set_terminal(AlgorithmState terminal) noexcept;

    std::string _plugin_path;
    std::string _worker_path;
    PluginCapability _capability;

    // ── Worker handles (see SandboxedAlgorithm::spawn_worker for lifecycle) ──
    int _fd = -1;       // read: worker→parent (Linux: full-duplex socketpair)
    int _write_fd = -1; // write: parent→worker (Windows separate pipe)
    [[maybe_unused]] int _stderr_fd = -1;
    [[maybe_unused]] long _pid = -1;
    [[maybe_unused]] void* _proc_handle = nullptr;
    [[maybe_unused]] void* _job_handle = nullptr;
    [[maybe_unused]] void* _stderr_handle = nullptr;

    std::string _name;
    std::string _version;
    AlgorithmMode _mode = AlgorithmMode::direct;
    bool _serializable = false;

    // ── Run state ───────────────────────────────────────────────────────
    mutable std::mutex _write_mtx; // serializes ALL parent→worker frames
    std::atomic<AlgorithmState> _state{AlgorithmState::Idle};
    std::atomic<double> _progress{0.0};
    mutable std::mutex _out_mtx; // guards _output
    AlgorithmOutput _output;     // authoritative result from MsgResult

    // Reader thread + serialize handshake (created fresh each run).
    std::shared_ptr<SerializeIntent> _serialize_intent;
    intptr_t _wake = -1; // eventfd (Linux) / event HANDLE (Windows)
    std::thread _reader;
};

} // namespace algorithm
