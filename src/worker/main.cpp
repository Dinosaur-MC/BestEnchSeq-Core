/// @file worker/main.cpp
/// besq-worker — sandboxed algorithm execution worker.
///
/// A dedicated process that dlopens a plugin and serves its IAlgorithm over
/// stdin/stdout IPC.  The parent (besq) spawns this with stdin/stdout wired
/// to a socketpair (Linux) / pipe pair (Windows), sends AlgorithmInput, and
/// receives streamed progress/solution events plus the final result.
///
/// Sandbox lifecycle (Linux):
///   1. parse --plugin <path> [--capability <level>]
///   2. dlopen + create the plugin's IAlgorithm
///   3. install seccomp filter (after dlopen — the .so is already mapped)
///   4. serve IPC frames on stdin/stdout
///
/// The worker links ONLY the algorithm kernel (besq-algo-core) + common.

#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/diagnostics/DiagnosticsService.h"
#include "domain/algorithm/diagnostics/IAlgorithmObserver.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/sandbox/IpcProtocol.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "common/io/ByteStream.h"
#include "sandbox_seccomp.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>
#endif

using namespace algorithm;

namespace {

// ── Plugin ABI (mirrors PluginAPI.h) ────────────────────────────────
using BesqCreateFn = void *(*)();

void *dl_open_plugin(const char *path) {
#if defined(_WIN32)
    return static_cast<void *>(::LoadLibraryA(path));
#else
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
void *dl_sym_plugin(void *handle, const char *sym) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(::GetProcAddress(static_cast<HMODULE>(handle), sym));
#else
    return ::dlsym(handle, sym);
#endif
}

// ── IPC forward observer: stream progress/solution back to the parent ──
// Attached to the worker's DiagnosticsService; runs on its event-loop thread.
class IpcForwardObserver : public IAlgorithmObserver {
  public:
    void on_progress(size_t /*task*/, uint8_t pct, ProgressStatus status) override {
        ByteStreamWriter w;
        w << pct << static_cast<uint8_t>(status);
        std::lock_guard lk(_mtx);
        ipc::write_frame(1, ipc::MsgType::MsgProgress, std::move(w).take());
    }
    void on_solution_found(size_t /*task*/, const std::vector<EnchStep> &solution) override {
        ByteStreamWriter w;
        w << solution;  // vector<EnchStep>
        std::lock_guard lk(_mtx);
        ipc::write_frame(1, ipc::MsgType::MsgSolution, std::move(w).take());
    }
    /// Write a frame under the same lock as the event callbacks, so the
    /// parent never sees interleaved frames (write_frame is two syscalls).
    void send_frame(ipc::MsgType type, const std::vector<uint8_t> &payload) {
        std::lock_guard lk(_mtx);
        ipc::write_frame(1, type, payload);
    }
  private:
    std::mutex _mtx;  // serializes all writes to the IPC channel
};

// ── Timeout-bounded frame read for the control thread (Linux) ───────
bool read_frame_timeout(int fd, ipc::MsgType &type, std::vector<uint8_t> &payload,
                        int timeout_ms) {
#if defined(__linux__)
    struct pollfd pfd{fd, POLLIN, 0};
    int rc = ::poll(&pfd, 1, timeout_ms);
    if (rc <= 0)
        return false;  // timeout or error
    return ipc::read_frame(fd, type, payload);
#else
    (void)fd; (void)type; (void)payload; (void)timeout_ms;
    return false;
#endif
}

void send_error(const std::string &msg) {
    ByteStreamWriter w;
    w << msg;
    ipc::write_frame(1, ipc::MsgType::MsgError, std::move(w).take());
}

// ── Handle MsgExecute: run the algorithm, stream events, reply done ──
void handle_execute(IAlgorithm &algo, const std::vector<uint8_t> &payload) {
    AlgorithmInput input;
    if (!ipc::decode(payload, input)) {
        send_error("besq-worker: malformed AlgorithmInput");
        return;
    }

    // Local ExecutionContext — entirely within this process (hot path, no IPC).
    std::string name(algo.name());
    ExecutionContext local_ctx(0, name.c_str());

    // Forward progress/solutions to the parent via DiagnosticsService.
    auto forwarder = IAlgorithmObserver::create<IpcForwardObserver>();

    // Control thread: receive cancel/pause/resume while execute() runs.
    std::atomic<bool> execute_done{false};
    std::thread control([&] {
        while (!execute_done.load(std::memory_order_acquire)) {
            ipc::MsgType type;
            std::vector<uint8_t> ctl;
            if (!read_frame_timeout(0, type, ctl, 50))
                continue;
            switch (type) {
            case ipc::MsgType::MsgCancel: local_ctx.cancel(); break;
            case ipc::MsgType::MsgPause:  local_ctx.pause();  break;
            case ipc::MsgType::MsgResume: local_ctx.resume(); break;
            default: break;
            }
        }
    });

    algo.init(input, local_ctx);
    algo.execute(input, local_ctx);

    execute_done.store(true, std::memory_order_release);
    control.join();

    // Flush queued progress/solution events BEFORE MsgResult, so the parent
    // never sees MsgResult followed by straggler solutions.  MsgResult is
    // written under the observer's lock to avoid frame interleaving.
    DiagnosticsService::instance().flush();
    forwarder->send_frame(ipc::MsgType::MsgResult, {});
}

// ── Main IPC service loop ───────────────────────────────────────────
void serve(IAlgorithm &algo) {
    for (;;) {
        ipc::MsgType type;
        std::vector<uint8_t> payload;
        if (!ipc::read_frame(0, type, payload))
            return;  // EOF (parent closed)

        switch (type) {
        case ipc::MsgType::MsgGetName: {
            ByteStreamWriter w; w << std::string(algo.name());
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgGetVersion: {
            ByteStreamWriter w; w << std::string(algo.version());
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgGetMode: {
            ByteStreamWriter w; w << static_cast<uint8_t>(algo.supported_mode());
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgEvaluate: {
            int16_t n = 0;
            if (payload.size() >= sizeof(n))
                std::memcpy(&n, payload.data(), sizeof(n));
            ByteStreamWriter w; w << algo.evaluate(n);
            ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            break;
        }
        case ipc::MsgType::MsgSimulate: {
            AlgorithmInput input;
            if (ipc::decode(payload, input)) {
                ByteStreamWriter w; w << algo.simulate(input);
                ipc::write_frame(1, ipc::MsgType::MsgResult, std::move(w).take());
            } else {
                send_error("besq-worker: malformed AlgorithmInput");
            }
            break;
        }
        case ipc::MsgType::MsgExecute:
            handle_execute(algo, payload);
            break;
        default:
            // Unknown / out-of-execute control messages are no-ops.
            break;
        }
    }
}

} // anonymous namespace

int main(int argc, char **argv) {
    const char *plugin_path = nullptr;
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
    void *handle = dl_open_plugin(plugin_path);
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

    std::unique_ptr<IAlgorithm> algo(static_cast<IAlgorithm *>(create_fn()));
    if (!algo) {
        std::fprintf(stderr, "besq-worker: plugin returned null\n");
        return 1;
    }

    // Diagnostics are forwarded over IPC, not persisted to files.
    DiagnosticsService::instance().set_persist(false);

    serve(*algo);
    return 0;
}
