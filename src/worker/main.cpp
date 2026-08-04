/// @file worker/main.cpp
/// besq-worker — sandboxed algorithm execution worker.
///
/// A dedicated process that dlopens a plugin and hosts a REAL AlgorithmExecutor
/// (the algorithm domain's authoritative engine) over stdin/stdout IPC.  The
/// sandbox seam lives ABOVE the executor: the executor, its ExecutionContext
/// and the plugin run fully locally in this process; the parent
/// (SandboxedExecutor) mirrors the executor's surface with coarse messages —
/// MsgRun/MsgResumeRun for lifecycle, MsgPause/Resume/Cancel for control,
/// MsgSerializeState for checkpoints, and a final MsgResult carrying the
/// encoded AlgorithmOutput.
///
/// Sandbox lifecycle (Linux):
///   1. parse --plugin <path>
///   2. dlopen + resolve besq_create_algorithm
///   3. install seccomp filter (after dlopen — the .so is already mapped)
///   4. construct the AlgorithmExecutor, serve IPC frames on stdin/stdout
///
/// The worker links ONLY the algorithm kernel (besq-algo-core) + common.

#include "common/io/ByteStream.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/diagnostics/DiagnosticsService.h"
#include "domain/algorithm/diagnostics/IAlgorithmObserver.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/sandbox/IpcProtocol.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "sandbox_seccomp.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

using namespace algorithm;

namespace {

// ── Plugin ABI (mirrors PluginAPI.h) ────────────────────────────────
using BesqCreateFn = void* (*)();

void* dl_open_plugin(const char* path) {
#if defined(_WIN32)
    return static_cast<void*>(::LoadLibraryA(path));
#else
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
void* dl_sym_plugin(void* handle, const char* sym) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), sym));
#else
    return ::dlsym(handle, sym);
#endif
}

// ── IPC forward observer: stream progress/solution back to the parent ──
// Attached to the worker's DiagnosticsService; runs on its event-loop thread.
// The mutex is the worker's SINGLE stdout write lock: the observer's events,
// the control thread's serialize reply and the serve thread's MsgResult all
// funnel through send_frame() so frames can never interleave.
class IpcForwardObserver : public IAlgorithmObserver {
public:
    void on_progress(size_t /*task*/, uint8_t pct, ProgressStatus status) override {
        ByteStreamWriter w;
        w << pct << static_cast<uint8_t>(status);
        send_frame(ipc::MsgType::MsgProgress, std::move(w).take());
    }
    void on_solution_found(size_t /*task*/, const std::vector<EnchStep>& solution) override {
        ByteStreamWriter w;
        w << solution; // vector<EnchStep>
        send_frame(ipc::MsgType::MsgSolution, std::move(w).take());
    }
    /// Write one frame under the shared lock (write_frame is two+ syscalls).
    void send_frame(ipc::MsgType type, const std::vector<uint8_t>& payload) {
        std::lock_guard lk(_mtx);
        ipc::write_frame(1, type, payload);
    }

private:
    std::mutex _mtx; // serializes ALL writes to the IPC channel
};

// ── Event-driven wakeup for the control thread ─────────────────────
// Two event sources: an IPC frame on stdin (cancel/pause/resume/serialize from
// the parent) OR the main thread finishing execute().  Linux: poll([stdin,
// exit eventfd], -1) — true kernel sleep.  Windows: anonymous pipes are NOT
// reliable wait objects (WFMO spuriously signals "readable" on an empty pipe),
// so stdin is probed with PeekNamedPipe (~1 ms); the exit event is a REAL
// waitable object so execute()-done still wakes instantly.
class ExitSignal {
public:
    ExitSignal() {
#if defined(__linux__)
        _fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
#elif defined(_WIN32)
        _evt = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
#endif
    }
    ~ExitSignal() {
#if defined(__linux__)
        if (_fd >= 0)
            ::close(_fd);
#elif defined(_WIN32)
        if (_evt)
            ::CloseHandle(_evt);
#endif
    }
#if defined(__linux__)
    int fd() const { return _fd; }
    void signal() {
        const uint64_t one = 1;
        if (_fd >= 0)
            (void)::write(_fd, &one, sizeof(one));
    }
#elif defined(_WIN32)
    HANDLE handle() const { return _evt; }
    void signal() {
        if (_evt)
            ::SetEvent(_evt);
    }
#endif
private:
#if defined(__linux__)
    int _fd = -1;
#elif defined(_WIN32)
    HANDLE _evt = nullptr;
#endif
};

/// Wait for either an IPC frame on stdin or the exit signal.
/// @return 0 = stdin readable, 1 = exit signal, -2 = nothing ready (retry),
///         -1 = error.
int wait_ipc_or_exit(const ExitSignal& exit_sig) {
#if defined(__linux__)
    struct pollfd pfds[2] = {{0, POLLIN, 0}, {exit_sig.fd(), POLLIN, 0}};
    int rc = ::poll(pfds, 2, -1);
    if (rc < 0)
        return -1;
    if (pfds[1].revents & POLLIN)
        return 1;
    return 0;
#elif defined(_WIN32)
    HANDLE stdin_h = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(stdin)));
    DWORD avail = 0;
    if (::PeekNamedPipe(stdin_h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
        return 0;
    if (::WaitForSingleObject(exit_sig.handle(), 1) == WAIT_OBJECT_0)
        return 1;
    return -2; // nothing ready — retry
#else
    (void)exit_sig;
    return -1;
#endif
}

void send_error(const std::string& msg) {
    ByteStreamWriter w;
    w << msg;
    ipc::write_frame(1, ipc::MsgType::MsgError, std::move(w).take());
}

// ── Handle MsgRun / MsgResumeRun: drive the executor to completion ──
void handle_run(AlgorithmExecutor& exec,
                bool resume,
                const std::vector<uint8_t>& payload,
                const std::shared_ptr<IpcForwardObserver>& forwarder) {
    // Control thread: receive cancel/pause/resume/serialize while execute()
    // runs.  Event-driven — sleeps in poll/WFMO until an IPC frame arrives OR
    // the main thread signals execute() is done.  It is the ONLY stdin reader
    // during the run (the serve thread is blocked in exec.wait()); it calls the
    // executor's own thread-safe control API, so no context-splitting is needed.
    ExitSignal exit_sig;
    std::thread control([&] {
        for (;;) {
            switch (wait_ipc_or_exit(exit_sig)) {
            case 1:
                return; // execute() finished — control no longer needed
            case -2:
                continue; // nothing ready — retry
            case -1:
                return; // poll/WFMO error
            default:
                break; // stdin readable
            }
            ipc::MsgType type;
            std::vector<uint8_t> ctl;
            if (!ipc::read_frame(0, type, ctl))
                return; // EOF (parent closed the channel)
            try {
                switch (type) {
                case ipc::MsgType::MsgCancel:
                    exec.cancel();
                    break;
                case ipc::MsgType::MsgPause:
                    // exec.pause() is synchronous: it blocks until the algorithm
                    // has actually quiesced at wait_if_paused() and only then flips
                    // to Paused.  Ack AFTER it returns, so the parent can mirror
                    // Pausing→Paused honestly (not merely "pause requested").
                    exec.pause();
                    forwarder->send_frame(ipc::MsgType::MsgPauseAck, {});
                    break;
                case ipc::MsgType::MsgResume:
                    exec.resume();
                    break;
                case ipc::MsgType::MsgSerializeState: {
                    // Only meaningful while paused (executor gates on its state);
                    // returns the full opaque checkpoint blob (large → auto-chunked).
                    // Dedicated MsgCheckpoint so the parent can never mistake the
                    // run-completion MsgResult for this reply.
                    const auto blob = exec.serialize_state();
                    forwarder->send_frame(ipc::MsgType::MsgCheckpoint, blob);
                    break;
                }
                default:
                    break;
                }
            } catch (const std::exception& e) {
                // Never let a control-API exception escape this thread — an
                // unhandled exception in a std::thread is std::terminate →
                // abort() → a debugger/JIT dialog, and the parent would only
                // see a dead worker.  Surface it as a frame instead.
                forwarder->send_frame(ipc::MsgType::MsgError,
                                      ipc::encode_value(std::string("besq-worker control: ") + e.what()));
            } catch (...) {
                forwarder->send_frame(ipc::MsgType::MsgError,
                                      ipc::encode_value(std::string("besq-worker control: unknown error")));
            }
        }
    });

    bool failed = false;
    try {
        if (resume) {
            exec.start(payload); // opaque checkpoint blob (input is embedded)
        } else {
            AlgorithmInput input;
            ByteStreamReader r(payload);
            input.deserialize(r);
            if (!r.ok())
                throw std::runtime_error("besq-worker: malformed AlgorithmInput");
            exec.start(std::move(input));
        }
        exec.wait();
    } catch (const std::exception& e) {
        failed = true;
        // Route through the forwarder's lock: the control thread may be
        // mid-write (serialize reply) when this fires, and all stdout writes
        // must share one mutex to keep frames from interleaving.
        forwarder->send_frame(ipc::MsgType::MsgError, ipc::encode_value(std::string("besq-worker: ") + e.what()));
    } catch (...) {
        failed = true;
        forwarder->send_frame(ipc::MsgType::MsgError, ipc::encode_value(std::string("besq-worker: unknown error")));
    }

    exit_sig.signal(); // wake the control thread to exit
    control.join();

    if (failed)
        return; // MsgError already sent

    // Flush queued progress/solution events BEFORE MsgResult, so the parent
    // never sees MsgResult followed by straggler solutions.
    DiagnosticsService::instance().flush();
    forwarder->send_frame(ipc::MsgType::MsgResult, ipc::encode_algorithm_output(exec.output()));
}

// ── Main IPC service loop ───────────────────────────────────────────
void serve(AlgorithmExecutor& exec) {
    for (;;) {
        ipc::MsgType type;
        std::vector<uint8_t> payload;
        if (!ipc::read_frame(0, type, payload))
            return; // EOF (parent closed)

        // Any handler exception must not escape main() — on Windows that is
        // std::terminate → abort() → a debugger/JIT dialog, blocking the parent
        // (which is waiting on this pipe) and reading as a hang.  Surface as a
        // frame instead.
        try {
        switch (type) {
        case ipc::MsgType::MsgGetName: {
            ByteStreamWriter w;
            w << std::string(exec.name());
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgGetVersion: {
            ByteStreamWriter w;
            w << std::string(exec.version());
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgGetMode: {
            ByteStreamWriter w;
            w << static_cast<uint8_t>(exec.supported_mode());
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgIsSerializable: {
            ByteStreamWriter w;
            w << exec.is_serializable();
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgEvaluate: {
            int16_t n = 0;
            if (payload.size() >= sizeof(n))
                std::memcpy(&n, payload.data(), sizeof(n));
            ByteStreamWriter w;
            w << exec.evaluate(n);
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgSimulate: {
            AlgorithmInput input;
            if (ipc::decode(payload, input)) {
                ByteStreamWriter w;
                w << exec.simulate(input);
                ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            } else {
                send_error("besq-worker: malformed AlgorithmInput");
            }
            break;
        }
        case ipc::MsgType::MsgRun: {
            auto forwarder = IAlgorithmObserver::create<IpcForwardObserver>();
            handle_run(exec, /*resume=*/false, payload, forwarder);
            break;
        }
        case ipc::MsgType::MsgResumeRun: {
            auto forwarder = IAlgorithmObserver::create<IpcForwardObserver>();
            handle_run(exec, /*resume=*/true, payload, forwarder);
            break;
        }
        default:
            // Unknown / out-of-run control messages are no-ops.
            break;
        }
        } catch (const std::exception& e) {
            send_error(std::string("besq-worker: ") + e.what());
        } catch (...) {
            send_error("besq-worker: unknown error");
        }
    }
}

} // anonymous namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // The IPC channel is stdin/stdout.  CRT defaults to TEXT mode, which
    // would CR/LF-mangle binary frames and treat 0x1A (Ctrl-Z) as EOF —
    // corrupting the protocol.  Force binary mode on all three std streams.
    ::_setmode(::_fileno(stdin), _O_BINARY);
    ::_setmode(::_fileno(stdout), _O_BINARY);
    ::_setmode(::_fileno(stderr), _O_BINARY);
#endif

    const char* plugin_path = nullptr;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--plugin") == 0)
            plugin_path = argv[i + 1];
    }
    if (!plugin_path) {
        std::fprintf(stderr, "usage: %s --plugin <path.so>\n", argv[0]);
        return 2;
    }

    // ── Load the plugin ──────────────────────────────────────────────
    // dlopen must run before seccomp (the dynamic loader needs open/mmap).
    // But seccomp is installed BEFORE besq_create_algorithm(), so the
    // plugin's constructor (and any code it runs) is already sandboxed.
    void* handle = dl_open_plugin(plugin_path);
    if (!handle) {
        std::fprintf(stderr, "besq-worker: failed to load plugin: %s\n", plugin_path);
        return 1;
    }
    auto create_fn = reinterpret_cast<BesqCreateFn>(dl_sym_plugin(handle, "besq_create_algorithm"));
    if (!create_fn) {
        std::fprintf(stderr, "besq-worker: missing besq_create_algorithm\n");
        return 1;
    }

    // ── Install seccomp AFTER dlopen, BEFORE create (Linux only) ─────
    if (!install_worker_seccomp()) {
        std::fprintf(stderr, "besq-worker: seccomp install failed\n");
        return 1;
    }

    std::unique_ptr<IAlgorithm> algo(static_cast<IAlgorithm*>(create_fn()));
    if (!algo) {
        std::fprintf(stderr, "besq-worker: plugin returned null\n");
        return 1;
    }
    // The executor IS the sandbox boundary now: it owns the plugin, the
    // ExecutionContext and the serializer — all local to this process.
    auto exec = std::make_unique<AlgorithmExecutor>(std::move(algo));

    // Diagnostics are forwarded over IPC, not persisted to files.
    DiagnosticsService::instance().set_persist(false);

    serve(*exec);
    return 0;
}
