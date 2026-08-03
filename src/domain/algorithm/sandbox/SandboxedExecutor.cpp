#include "domain/algorithm/sandbox/SandboxedExecutor.h"
#include "AppConfig.h"
#include "common/log/log.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#if defined(__linux__)
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace algorithm {

namespace {

/// Directory containing the current executable (empty if unavailable).
std::string current_exe_dir() {
#if defined(__linux__)
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#elif defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0)
        return {};
    return std::filesystem::path(buf).parent_path().string();
#else
    return {};
#endif
}

/// Worker binary name suffix (".exe" on Windows, "" elsewhere).
const char* worker_exe_suffix() {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

/// AppConfig / exe-dir / PATH worker resolution.
std::string resolve_worker_path(const std::string& given) {
    std::string result;
    if (!given.empty()) {
        result = given;
    } else {
        // Global app config (BESQ_WORKER_PATH), from the shared AppConfig singleton.
        const std::string cfg_path = AppConfig::get().sandbox_worker_path;
        if (!cfg_path.empty()) {
            result = cfg_path;
        } else {
            // The worker ships alongside the host executable — try there first so
            // BESQ_SANDBOX=1 works without the worker on PATH.
            const std::string dir = current_exe_dir();
            if (!dir.empty()) {
                const std::string candidate = dir + "/besq-worker" + worker_exe_suffix();
                if (std::filesystem::exists(candidate))
                    result = candidate;
            }
            if (result.empty())
                result = "besq-worker"; // last resort: PATH
        }
    }
    // Absolutize any path with a directory component so CreateProcess (Windows)
    // resolves it regardless of the caller's cwd; keep a bare name bare so it
    // can still be found on PATH.
    const bool has_sep = result.find('/') != std::string::npos || result.find('\\') != std::string::npos;
    if (!has_sep)
        return result;
    std::error_code ec;
    const auto abs = std::filesystem::absolute(result, ec);
    return ec ? result : abs.string();
}

} // anonymous namespace

SandboxedExecutor::SandboxedExecutor(std::string plugin_path, std::string worker_path, PluginCapability capability)
    : _plugin_path(std::move(plugin_path)), _worker_path(resolve_worker_path(std::move(worker_path))), _capability(capability) {
#if defined(__linux__)
    // A dead worker makes the socket write return EPIPE — convert it to a
    // throwable error instead of SIGPIPE-killing the whole parent process.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    spawn_worker();
    try {
        query_metadata();
    } catch (...) {
        // Partial construction: the destructor never runs, so kill the spawned
        // worker and close the fds/handles before rethrowing.  (No reader
        // thread exists yet — start() hasn't been called.)
        teardown_worker();
        throw;
    }
}

SandboxedExecutor::~SandboxedExecutor() {
    // Forward a best-effort cancel so a mid-run worker stops promptly.
    try {
        cancel();
    } catch (...) {
    }
    // Force the reader thread to exit promptly, so the join below cannot hang
    // even if the worker ignores cancel() — precisely the malicious-plugin
    // threat model this sandbox exists to contain.  Never throw from a dtor.
    _shutdown.store(true, std::memory_order_release);
    wake_reader();
#if defined(__linux__)
    // Unblock the reader's poll/read immediately: shutdown() the read side of
    // the socketpair makes pending reads return EOF.  The fd number stays
    // valid, so the reader cannot read from a recycled descriptor.
    if (_fd >= 0)
        ::shutdown(_fd, SHUT_RD);
#endif
    if (_reader.joinable())
        _reader.join();
    teardown_worker();
}

void SandboxedExecutor::teardown_worker() noexcept {
#if defined(__linux__)
    if (_wake >= 0) {
        ::close(static_cast<int>(_wake));
        _wake = -1;
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
    if (_stderr_fd >= 0) {
        ::close(_stderr_fd);
        _stderr_fd = -1;
    }
    if (_pid > 0) {
        // Kill the whole worker process group (covers plugin-forked children
        // that might keep the IPC channel open).
        ::kill(-_pid, SIGKILL);
        ::waitpid(_pid, nullptr, 0);
    }
#elif defined(_WIN32)
    if (_wake) {
        ::CloseHandle(reinterpret_cast<HANDLE>(_wake));
        _wake = -1;
    }
    if (_proc_handle) {
        ::TerminateProcess(static_cast<HANDLE>(_proc_handle), 1);
        ::WaitForSingleObject(static_cast<HANDLE>(_proc_handle), INFINITE);
        ::CloseHandle(static_cast<HANDLE>(_proc_handle));
        _proc_handle = nullptr;
    }
    if (_job_handle) {
        ::CloseHandle(static_cast<HANDLE>(_job_handle)); // KILL_ON_JOB_CLOSE
        _job_handle = nullptr;
    }
    if (_stderr_handle) {
        ::CloseHandle(static_cast<HANDLE>(_stderr_handle));
        _stderr_handle = nullptr;
    }
    if (_fd >= 0) {
        ::_close(_fd);
        _fd = -1;
    }
    if (_write_fd >= 0) {
        ::_close(_write_fd);
        _write_fd = -1;
    }
    if (_stderr_fd >= 0) {
        ::_close(_stderr_fd);
        _stderr_fd = -1;
    }
#endif
}

// ── IExecutor metadata / preflight ─────────────────────────────────

bool SandboxedExecutor::simulate(const AlgorithmInput& input) const noexcept {
    // Preflight only: during a run the reader thread owns the pipe; a second
    // reader would corrupt frames.  Reject silently outside Idle (Idle is the
    // only state from which start() may be called, so this is a safe guard).
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Idle)
        return false;
    try {
        send(ipc::MsgType::MsgSimulate, ipc::encode(input));
        ipc::MsgType type;
        std::vector<uint8_t> result;
        if (!recv(type, result))
            return false;
        bool v = false;
        ByteStreamReader r(result);
        r >> v;
        return r.ok() && v;
    } catch (...) {
        return false;
    }
}

double SandboxedExecutor::evaluate(int16_t ench_count) const noexcept {
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Idle)
        return 0.0;
    try {
        const auto payload = ipc::encode_value(ench_count);
        send(ipc::MsgType::MsgEvaluate, payload);
        ipc::MsgType type;
        std::vector<uint8_t> result;
        if (!recv(type, result))
            return 0.0;
        double v = 0.0;
        ByteStreamReader r(result);
        r >> v;
        return r.ok() ? v : 0.0;
    } catch (...) {
        return 0.0;
    }
}

// ── Run lifecycle ─────────────────────────────────────────────────

void SandboxedExecutor::start(AlgorithmInput input) {
    const AlgorithmState prev = _state.load(std::memory_order_acquire);
    if (prev == AlgorithmState::Running || prev == AlgorithmState::Paused)
        throw std::logic_error("sandboxed executor already running or paused");
    if (prev == AlgorithmState::Cancelled || prev == AlgorithmState::Failed)
        throw std::logic_error("sandboxed executor in terminal state");

    ByteStreamWriter w;
    input.serialize(w);
    send(ipc::MsgType::MsgRun, std::move(w).take());
    begin_run();
}

void SandboxedExecutor::start(const std::vector<uint8_t>& checkpoint) {
    if (checkpoint.empty())
        throw std::invalid_argument("empty checkpoint");
    const AlgorithmState prev = _state.load(std::memory_order_acquire);
    if (prev == AlgorithmState::Running || prev == AlgorithmState::Paused)
        throw std::logic_error("sandboxed executor already running or paused");
    if (prev == AlgorithmState::Cancelled || prev == AlgorithmState::Failed)
        throw std::logic_error("sandboxed executor in terminal state");

    send(ipc::MsgType::MsgResumeRun, checkpoint);
    begin_run();
}

void SandboxedExecutor::begin_run() {
    // A previous run's reader may still be joinable (wait() not yet called);
    // it has already returned after MsgResult, so joining is safe and instant.
    if (_reader.joinable())
        _reader.join();
    {
        std::lock_guard lk(_out_mtx);
        _output = AlgorithmOutput{};
    }
    _progress.store(0.0, std::memory_order_release);
    _serialize_intent = std::make_shared<SerializeIntent>();
    _state.store(AlgorithmState::Running, std::memory_order_release);
    _reader = std::thread([this] { reader_loop(); });
}

void SandboxedExecutor::pause() {
    // Mirror in-process: a pause before start() is a no-op.
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Running)
        return;
    send(ipc::MsgType::MsgPause, {});
    _state.store(AlgorithmState::Paused, std::memory_order_release);
}

void SandboxedExecutor::resume() {
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Paused)
        return;
    send(ipc::MsgType::MsgResume, {});
    _state.store(AlgorithmState::Running, std::memory_order_release);
}

void SandboxedExecutor::cancel() {
    const AlgorithmState prev = _state.exchange(AlgorithmState::Cancelled, std::memory_order_acq_rel);
    // Don't clobber Completed/Failed (results would be lost/mislabeled); Idle
    // is restored too so a pre-start cancel() cannot brick later start()s.
    if (prev == AlgorithmState::Completed || prev == AlgorithmState::Failed || prev == AlgorithmState::Idle) {
        _state.store(prev, std::memory_order_release);
        return;
    }
    // Running or Paused → forward to the worker; its executor.cancel() also
    // resumes a paused run internally so the algorithm observes the flag.
    send(ipc::MsgType::MsgCancel, {});
}

AlgorithmState SandboxedExecutor::wait() {
    if (_reader.joinable())
        _reader.join();
    return _state.load(std::memory_order_acquire);
}

AlgorithmOutput SandboxedExecutor::output() const {
    std::lock_guard lk(_out_mtx);
    return _output;
}

std::vector<uint8_t> SandboxedExecutor::serialize_state() const {
    // Only meaningful while paused (worker executor is quiescent) — same gate
    // as AlgorithmExecutor::serialize_state().
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Paused)
        return {};
    auto intent = _serialize_intent;
    if (!intent)
        return {};
    {
        std::lock_guard lk(intent->mtx);
        intent->pending = true;
        intent->error = false;
        intent->result.clear();
    }
    wake_reader();
    std::unique_lock lk(intent->mtx);
    intent->cv.wait(lk, [&] { return !intent->pending; });
    if (intent->error)
        return {};
    return intent->result;
}

// ── Reader thread: the single consumer of the worker→parent channel ──

void SandboxedExecutor::reader_loop() noexcept {
    for (;;) {
        // Destructor requested prompt exit — leave without touching state.
        if (_shutdown.load(std::memory_order_acquire)) {
            abort_pending_serialize();
            return;
        }
        ipc::MsgType type = ipc::MsgType::MsgResult;
        std::vector<uint8_t> payload;
        bool got = false;
        bool woken = false;
#if defined(__linux__)
        // Wait on BOTH sources: worker IPC data + the serialize wake event.
        // Zero polling — the kernel wakes poll() on either.
        struct pollfd pfds[2] = {{_fd, POLLIN, 0}, {static_cast<int>(_wake), POLLIN, 0}};
        int rc = ::poll(pfds, 2, -1);
        if (rc < 0) {
            set_terminal(AlgorithmState::Failed);
            abort_pending_serialize();
            return;
        }
        if (pfds[1].revents & POLLIN) {
            uint64_t v;
            (void)::read(static_cast<int>(_wake), &v, sizeof(v)); // consume counter
            woken = true;
        } else if (pfds[0].revents & POLLIN) {
            got = ipc::read_frame(_fd, type, payload);
        }
#elif defined(_WIN32)
        // Windows anonymous pipes are NOT reliable wait objects — WFMO on the
        // read handle spuriously signals "readable" on an empty pipe and then
        // read_frame would block forever.  Detect data with PeekNamedPipe
        // (non-blocking); the wake event is a REAL waitable object.
        HANDLE pipe_h = reinterpret_cast<HANDLE>(::_get_osfhandle(_fd));
        DWORD avail = 0;
        if (::PeekNamedPipe(pipe_h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            got = ipc::read_frame(_fd, type, payload);
        } else if (::WaitForSingleObject(reinterpret_cast<HANDLE>(_wake), 1) == WAIT_OBJECT_0) {
            ::ResetEvent(reinterpret_cast<HANDLE>(_wake));
            woken = true;
        } else {
            continue; // nothing ready — re-poll (≤1 ms); _shutdown checked at top
        }
#else
        got = ipc::read_frame(_fd, type, payload);
#endif

        if (woken) {
            // Serialize handshake: the reader owns the pipe, so it performs
            // the MsgSerializeState exchange and delivers the opaque blob.
            auto intent = _serialize_intent;
            bool do_serialize = false;
            if (intent) {
                std::lock_guard lk(intent->mtx);
                do_serialize = intent->pending;
            }
            if (do_serialize) {
                std::vector<uint8_t> blob;
                bool ok = false;
                bool run_ended = false; // worker sent a run-completion MsgResult
                bool eof = false;       // worker died mid-handshake
                try {
                    send(ipc::MsgType::MsgSerializeState, {});
                    // The reply is a DEDICATED MsgCheckpoint — unambiguous
                    // against the run-completion MsgResult that can share the
                    // wire when a cancel()/completion races the handshake.
                    // Skip straggler progress/solution frames queued before
                    // the worker paused.
                    for (;;) {
                        ipc::MsgType t;
                        std::vector<uint8_t> p;
                        if (!recv(t, p)) {
                            eof = true;
                            break; // EOF — worker died mid-serialize
                        }
                        if (t == ipc::MsgType::MsgCheckpoint) {
                            blob = std::move(p);
                            ok = true;
                            break;
                        }
                        if (t == ipc::MsgType::MsgResult) {
                            // The run ended while we were serializing — consume
                            // the terminal result instead of the checkpoint and
                            // exit with the true state.
                            AlgorithmOutput out;
                            if (ipc::decode_algorithm_output(p, out)) {
                                std::lock_guard lk(_out_mtx);
                                _output = std::move(out);
                            }
                            run_ended = true;
                            ok = false;
                            break;
                        }
                        if (t == ipc::MsgType::MsgError) {
                            ok = false;
                            break;
                        }
                    }
                } catch (...) {
                    ok = false;
                }
                {
                    std::lock_guard lk(intent->mtx);
                    intent->result = std::move(blob);
                    intent->error = !ok;
                    intent->pending = false;
                    intent->cv.notify_all();
                }
                if (run_ended) {
                    set_terminal(AlgorithmState::Completed);
                    return;
                }
                if (eof) {
                    set_terminal(AlgorithmState::Failed);
                    return;
                }
            }
            continue;
        }

        if (!got) {
            set_terminal(AlgorithmState::Failed); // worker closed the channel
            abort_pending_serialize();
            return;
        }

        switch (type) {
        case ipc::MsgType::MsgProgress: {
            ByteStreamReader r(payload);
            uint8_t pct = 0, status = 0;
            r >> pct >> status;
            if (r.ok())
                _progress.store(pct / 100.0, std::memory_order_release);
            break;
        }
        case ipc::MsgType::MsgSolution:
            // The authoritative solutions arrive in the final MsgResult; the
            // streamed copy is only for live UIs, which we don't drive here.
            break;
        case ipc::MsgType::MsgCheckpoint:
            // Worker sends this only in reply to MsgSerializeState (handled
            // above); outside a handshake it's a stray — ignore.
            break;
        case ipc::MsgType::MsgResult: {
            AlgorithmOutput out;
            if (ipc::decode_algorithm_output(payload, out)) {
                std::lock_guard lk(_out_mtx);
                _output = std::move(out);
            }
            set_terminal(AlgorithmState::Completed);
            abort_pending_serialize(); // natural completion raced a serialize intent
            return;
        }
        case ipc::MsgType::MsgError: {
            // Keep Cancelled if the run was cancelled; otherwise Failed.
            set_terminal(AlgorithmState::Failed);
            abort_pending_serialize();
            return;
        }
        default:
            break;
        }
    }
}

void SandboxedExecutor::abort_pending_serialize() noexcept {
    auto intent = _serialize_intent;
    if (!intent)
        return;
    std::lock_guard lk(intent->mtx);
    if (intent->pending) {
        intent->error = true;
        intent->pending = false;
        intent->cv.notify_all();
    }
}

void SandboxedExecutor::set_terminal(AlgorithmState terminal) noexcept {
    const AlgorithmState prev = _state.exchange(terminal, std::memory_order_acq_rel);
    // Cancelled is terminal and wins — a concurrent cancel() must not be
    // clobbered by a late Completed/Failed from the reader.
    if (prev == AlgorithmState::Cancelled)
        _state.store(AlgorithmState::Cancelled, std::memory_order_release);
}

// ── IPC helpers ───────────────────────────────────────────────────

void SandboxedExecutor::send(ipc::MsgType type, const std::vector<uint8_t>& payload) const {
    // All parent→worker frames share one write lock: start() (caller thread),
    // pause/resume/cancel (any thread) and the reader's serialize request can
    // be concurrent, and write_frame on a large payload is multiple syscalls.
    std::lock_guard lk(_write_mtx);
    const int fd = _write_fd >= 0 ? _write_fd : _fd;
    if (!ipc::write_frame(fd, type, payload))
        throw std::runtime_error("sandbox: worker IPC write failed");
}

bool SandboxedExecutor::recv(ipc::MsgType& type, std::vector<uint8_t>& payload) const {
    return ipc::read_frame(_fd, type, payload);
}

void SandboxedExecutor::wake_reader() const noexcept {
#if defined(__linux__)
    const uint64_t one = 1;
    if (_wake >= 0)
        (void)::write(static_cast<int>(_wake), &one, sizeof(one));
#elif defined(_WIN32)
    if (_wake)
        (void)::SetEvent(reinterpret_cast<HANDLE>(_wake));
#endif
}

// ── Worker spawn / metadata ───────────────────────────────────────

void SandboxedExecutor::spawn_worker() {
#if defined(__linux__)
    // CLOEXEC on both channels: the forked child closes them via dup2 anyway,
    // but without CLOEXEC any OTHER fd the parent has open (e.g. a second
    // SandboxedExecutor's socketpair, if lifetimes overlap) leaks into the
    // worker process and could keep that pipe's read end open forever.
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0)
        throw std::runtime_error("sandbox: socketpair failed");

    // Capture the worker's stderr so plugin/worker diagnostics surface to
    // the parent (and the sandbox test can assert on them).
    int err_pipe[2];
    if (::pipe(err_pipe) != 0) {
        ::close(sv[0]);
        ::close(sv[1]);
        throw std::runtime_error("sandbox: stderr pipe failed");
    }
    // Set CLOEXEC on both ends via fcntl — pipe2(O_CLOEXEC) needs _GNU_SOURCE,
    // which the project's default feature macros don't define.
    ::fcntl(err_pipe[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(sv[0]);
        ::close(sv[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        throw std::runtime_error("sandbox: fork failed");
    }
    if (pid == 0) {
        // Child: stdin/stdout are the IPC channel, stderr → pipe.  Become a
        // process-group leader so the parent can killpg() the whole group
        // (worker + any plugin-forked descendants) instead of just the child.
        ::setpgid(0, 0);
        ::prctl(PR_SET_PDEATHSIG, SIGKILL); // no orphans if the parent dies
        ::dup2(sv[1], 0);
        ::dup2(sv[1], 1);
        ::dup2(err_pipe[1], 2);
        ::close(sv[0]);
        ::close(sv[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        ::execl(_worker_path.c_str(), "besq-worker", "--plugin", _plugin_path.c_str(), (const char*)nullptr);
        ::_exit(127);
    }
    ::close(sv[1]);
    ::close(err_pipe[1]);
    _fd = sv[0];
    _stderr_fd = err_pipe[0];
    _pid = static_cast<long>(pid);
#elif defined(_WIN32)
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE}; // inheritable

    HANDLE to_child_rd = nullptr, to_child_wr = nullptr;     // parent→child (child stdin)
    HANDLE from_child_rd = nullptr, from_child_wr = nullptr; // child→parent (child stdout)
    HANDLE err_rd = nullptr, err_wr = nullptr;               // child stderr
    if (!::CreatePipe(&to_child_rd, &to_child_wr, &sa, 0) || !::CreatePipe(&from_child_rd, &from_child_wr, &sa, 0) ||
        !::CreatePipe(&err_rd, &err_wr, &sa, 0))
        throw std::runtime_error("sandbox: CreatePipe failed");
    // Parent's ends must not be inherited by the child.
    ::SetHandleInformation(to_child_wr, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(from_child_rd, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = to_child_rd;
    si.hStdOutput = from_child_wr;
    si.hStdError = err_wr;

    // Quote both paths (may contain spaces).
    const std::string cmdline = "\"" + _worker_path + "\" --plugin \"" + _plugin_path + "\"";
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessA(nullptr, const_cast<char*>(cmdline.c_str()), nullptr, nullptr, TRUE /*inherit handles*/, 0, nullptr,
                          nullptr, &si, &pi)) {
        ::CloseHandle(to_child_rd);
        ::CloseHandle(to_child_wr);
        ::CloseHandle(from_child_rd);
        ::CloseHandle(from_child_wr);
        ::CloseHandle(err_rd);
        ::CloseHandle(err_wr);
        const DWORD err = ::GetLastError();
        char ebuf[160] = {};
        ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0, ebuf, sizeof(ebuf),
                         nullptr);
        throw std::runtime_error(std::string("sandbox: CreateProcess failed (") + std::to_string(err) + "): " + ebuf);
    }
    ::CloseHandle(to_child_rd);
    ::CloseHandle(from_child_wr);
    ::CloseHandle(err_wr);
    ::CloseHandle(pi.hThread);

    // Job Object: kill-on-close + memory/process limits (resource containment —
    // Windows has no seccomp; process isolation + these limits are the sandbox).
    HANDLE job = ::CreateJobObjectA(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                                JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
        jeli.BasicLimitInformation.ActiveProcessLimit = 1;
        jeli.ProcessMemoryLimit = 512u * 1024 * 1024; // 512 MB
        jeli.JobMemoryLimit = 512u * 1024 * 1024;
        if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
            LOG_WARN("sandbox: SetInformationJobObject failed — worker has no resource limits");
        if (!::AssignProcessToJobObject(job, pi.hProcess))
            LOG_WARN("sandbox: AssignProcessToJobObject failed — worker NOT in Job Object");
    }

    _fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(from_child_rd), _O_RDONLY | _O_BINARY);
    _write_fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(to_child_wr), _O_WRONLY | _O_BINARY);
    _stderr_handle = err_rd; // raw handle for PeekNamedPipe-based stderr drain
    _pid = static_cast<long>(pi.dwProcessId);
    _proc_handle = pi.hProcess;
    _job_handle = job;
#else
    (void)0;
    throw std::runtime_error("sandbox: subprocess isolation not supported on this platform");
#endif

    // Reader wake handle (per-executor, reused across runs).
#if defined(__linux__)
    _wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
#elif defined(_WIN32)
    _wake = reinterpret_cast<intptr_t>(::CreateEventA(nullptr, TRUE, FALSE, nullptr));
#endif
}

void SandboxedExecutor::query_metadata() {
    ipc::MsgType type;
    std::vector<uint8_t> result;

    send(ipc::MsgType::MsgGetName, {});
    if (!recv(type, result)) {
        // The worker likely died at startup (plugin load failure, seccomp, …).
        // Drain its stderr so the reason surfaces instead of a bare EOF.
        const std::string err = take_worker_stderr();
        throw std::runtime_error("sandbox: worker get_name failed" + (err.empty() ? std::string{} : ": " + err));
    }
    {
        ByteStreamReader r(result);
        r >> _name;
    }

    send(ipc::MsgType::MsgGetVersion, {});
    if (!recv(type, result))
        throw std::runtime_error("sandbox: worker get_version failed");
    {
        ByteStreamReader r(result);
        r >> _version;
    }

    send(ipc::MsgType::MsgGetMode, {});
    if (!recv(type, result))
        throw std::runtime_error("sandbox: worker get_mode failed");
    {
        ByteStreamReader r(result);
        uint8_t m = 0;
        r >> m;
        _mode = static_cast<AlgorithmMode>(m);
    }

    send(ipc::MsgType::MsgIsSerializable, {});
    if (!recv(type, result))
        throw std::runtime_error("sandbox: worker is_serializable failed");
    {
        ByteStreamReader r(result);
        r >> _serializable;
    }
}

std::string SandboxedExecutor::take_worker_stderr() {
#if defined(__linux__)
    if (_stderr_fd < 0)
        return {};
    // Non-blocking drain: the worker stays alive with its stderr write end
    // open, so a blocking read would hang forever waiting for EOF.  Poll with
    // a zero timeout and read only what is already buffered.
    std::string out;
    char buf[512];
    for (;;) {
        struct pollfd pfd{_stderr_fd, POLLIN, 0};
        int rc = ::poll(&pfd, 1, 0);
        if (rc <= 0)
            break; // nothing buffered / error
        ssize_t n = ::read(_stderr_fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
#elif defined(_WIN32)
    if (!_stderr_handle)
        return {};
    // PeekNamedPipe + ReadFile: read only what is already buffered.
    std::string out;
    char buf[512];
    DWORD avail = 0;
    while (_stderr_handle && ::PeekNamedPipe(_stderr_handle, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        DWORD got = 0;
        if (!::ReadFile(_stderr_handle, buf, static_cast<DWORD>(std::min<size_t>(sizeof(buf), avail)), &got, nullptr) ||
            got == 0)
            break;
        out.append(buf, static_cast<size_t>(got));
    }
    return out;
#else
    return {};
#endif
}

} // namespace algorithm
