#include "domain/algorithm/sandbox/SandboxedAlgorithm.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "AppConfig.h"
#include "common/io/ByteStream.h"
#include "common/log/log.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#if defined(__linux__)
#include <csignal>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
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
const char *worker_exe_suffix() {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

/// AppConfig / exe-dir / PATH worker resolution.
std::string resolve_worker_path(const std::string &given) {
    if (!given.empty())
        return given;
    // Global app config (BESQ_WORKER_PATH), from the shared AppConfig singleton.
    const std::string cfg_path = AppConfig::get().sandbox_worker_path;
    if (!cfg_path.empty())
        return cfg_path;
    // The worker ships alongside the host executable — try there first so
    // BESQ_SANDBOX=1 works without the worker on PATH.
    const std::string dir = current_exe_dir();
    if (!dir.empty()) {
        const std::string candidate = dir + "/besq-worker" + worker_exe_suffix();
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return "besq-worker";  // last resort: PATH
}

/// Owns the parent-side cancellation wakeup for ONE execute() call.  On Linux
/// it's an eventfd (poll sees POLLIN); on Windows a manual-reset event
/// (WaitForSingleObject sees it).  notify() is the ExecutionContext cancel
/// hook; the destructor closes the underlying handle.  The notifier is held by
/// an atomic shared_ptr, so the fd/event cannot leak across solves and cannot
/// be used-after-free while a concurrent cancel() still references it.
class ExecuteCancelNotifier : public CancellationNotifier {
  public:
    ExecuteCancelNotifier() {
#if defined(__linux__)
        _fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
#elif defined(_WIN32)
        _evt = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
#endif
    }
    ~ExecuteCancelNotifier() override {
#if defined(__linux__)
        if (_fd >= 0) ::close(_fd);
#elif defined(_WIN32)
        if (_evt) ::CloseHandle(_evt);
#endif
    }
    [[nodiscard]] bool valid() const noexcept {
#if defined(__linux__)
        return _fd >= 0;
#elif defined(_WIN32)
        return _evt != nullptr;
#else
        return false;
#endif
    }
    void notify() noexcept override {
#if defined(__linux__)
        const uint64_t one = 1;
        if (_fd >= 0) (void)::write(_fd, &one, sizeof(one));
#elif defined(_WIN32)
        if (_evt) ::SetEvent(_evt);
#endif
    }
#if defined(__linux__)
    int fd() const noexcept { return _fd; }
#elif defined(_WIN32)
    HANDLE handle() const noexcept { return _evt; }
#endif
  private:
#if defined(__linux__)
    int _fd = -1;
#elif defined(_WIN32)
    HANDLE _evt = nullptr;
#endif
};

} // anonymous namespace

SandboxedAlgorithm::SandboxedAlgorithm(std::string plugin_path, std::string worker_path,
                                       PluginCapability capability)
    : _plugin_path(std::move(plugin_path)),
      _worker_path(resolve_worker_path(std::move(worker_path))),
      _capability(capability) {
#if defined(__linux__)
    // A dead worker makes the socket write return EPIPE — convert it to a
    // throwable error instead of SIGPIPE-killing the whole parent process.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    spawn_worker();
    query_metadata();
}

SandboxedAlgorithm::~SandboxedAlgorithm() {
#if defined(__linux__)
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
    if (_proc_handle) {
        ::TerminateProcess(static_cast<HANDLE>(_proc_handle), 1);
        ::WaitForSingleObject(static_cast<HANDLE>(_proc_handle), INFINITE);
        ::CloseHandle(static_cast<HANDLE>(_proc_handle));
        _proc_handle = nullptr;
    }
    if (_job_handle) {
        ::CloseHandle(static_cast<HANDLE>(_job_handle));  // KILL_ON_JOB_CLOSE
        _job_handle = nullptr;
    }
    if (_stderr_handle) {
        ::CloseHandle(static_cast<HANDLE>(_stderr_handle));
        _stderr_handle = nullptr;
    }
    if (_fd >= 0) { ::_close(_fd); _fd = -1; }
    if (_write_fd >= 0) { ::_close(_write_fd); _write_fd = -1; }
    if (_stderr_fd >= 0) { ::_close(_stderr_fd); _stderr_fd = -1; }
#endif
}

// ── IAlgorithm ─────────────────────────────────────────────────────

double SandboxedAlgorithm::evaluate(int16_t ench_count) const noexcept {
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

bool SandboxedAlgorithm::simulate(const AlgorithmInput &input) const noexcept {
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

void SandboxedAlgorithm::execute(const AlgorithmInput &input, ExecutionContext &ctx) {
    send(ipc::MsgType::MsgExecute, ipc::encode(input));

    // Event-driven wakeup for parent-side cancellation: the executor's
    // timeout watcher / signal handler calls ctx.cancel() on another thread.
    // The notifier OWNS the eventfd / Win32 event and closes it when the last
    // reference drops (after execute() and any in-flight cancel() finish) —
    // no per-solve fd/handle leak, no use-after-free.
    auto notifier = std::make_shared<ExecuteCancelNotifier>();
    if (!notifier->valid())
        throw std::runtime_error("sandboxed execute: cancel notifier creation failed");
    ctx.set_cancel_notifier(notifier);

    // Race guard: the executor may have cancelled between MsgExecute and the
    // hook install above (the event would then never fire).  Re-check once.
    if (ctx.is_cancelled())
        send(ipc::MsgType::MsgCancel, {});

    try {
        bool done = false;
        while (!done) {
            ipc::MsgType type;
            std::vector<uint8_t> payload;
            bool got = false;
#if defined(__linux__)
            // Wait on BOTH event sources with an infinite timeout: the worker's
            // IPC channel (data → kernel wakes poll) and the cancel eventfd
            // (cancel → cancel_notify writes → poll wakes).  Zero polling.
            struct pollfd pfds[2] = {{_fd, POLLIN, 0}, {notifier->fd(), POLLIN, 0}};
            int rc = ::poll(pfds, 2, -1);
            if (rc < 0)
                throw std::runtime_error("sandboxed execute: poll failed");
            if (pfds[1].revents & POLLIN) {
                uint64_t v;
                (void)::read(notifier->fd(), &v, sizeof(v));  // consume the counter
                send(ipc::MsgType::MsgCancel, {});
                continue;
            }
            got = ipc::read_frame(_fd, type, payload);
#elif defined(_WIN32)
            // Windows anonymous pipes are NOT reliable wait objects — WFMO
            // on the read handle spuriously signals "readable" on an empty
            // pipe and read_frame would then block forever.  Detect data
            // with PeekNamedPipe (non-blocking); the cancel event is a REAL
            // waitable object, so an executor cancel still wakes us instantly.
            HANDLE pipe_h = reinterpret_cast<HANDLE>(::_get_osfhandle(_fd));
            DWORD avail = 0;
            if (::PeekNamedPipe(pipe_h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                got = ipc::read_frame(_fd, type, payload);
            } else if (::WaitForSingleObject(notifier->handle(), 1) == WAIT_OBJECT_0) {
                ::ResetEvent(notifier->handle());
                send(ipc::MsgType::MsgCancel, {});
                continue;
            } else {
                continue;  // nothing ready — re-poll (≤1 ms)
            }
#else
            got = ipc::read_frame(_fd, type, payload);
#endif
            if (!got)
                throw std::runtime_error("sandboxed execute: worker closed channel");

            switch (type) {
            case ipc::MsgType::MsgProgress: {
                ByteStreamReader r(payload);
                uint8_t pct = 0, status = 0;
                r >> pct >> status;
                if (r.ok())
                    ctx.report_progress(pct, static_cast<ProgressStatus>(status));
                break;
            }
            case ipc::MsgType::MsgSolution: {
                std::vector<EnchStep> steps;
                ByteStreamReader r(payload);
                r >> steps;
                if (r.ok())
                    ctx.report_solution(std::move(steps));
                break;
            }
            case ipc::MsgType::MsgResult:
                done = true;
                break;
            case ipc::MsgType::MsgError: {
                ByteStreamReader r(payload);
                std::string msg;
                r >> msg;
                throw std::runtime_error(msg.empty() ? "sandboxed execute failed" : msg);
            }
            default:
                break;
            }
        }
    } catch (...) {
        // Drop our reference; the notifier (and its fd/event) is freed once no
        // in-flight cancel() holds it — a late cancel never touches a freed handle.
        ctx.set_cancel_notifier(nullptr);
        throw;
    }
    ctx.set_cancel_notifier(nullptr);
}

std::unique_ptr<IForgeEngine> SandboxedAlgorithm::get_forge_engine() const noexcept {
    // v1: the worker computes results internally; this default engine only
    // serves IAlgorithm::process() replay in the parent (vanilla rules).
    return std::make_unique<ForgeEngine>();
}

// ── Private helpers ─────────────────────────────────────────────────

void SandboxedAlgorithm::spawn_worker() {
#if defined(__linux__)
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        throw std::runtime_error("sandbox: socketpair failed");

    // Capture the worker's stderr so plugin/worker diagnostics surface to
    // the parent (and the sandbox test can assert on them).
    int err_pipe[2];
    if (::pipe(err_pipe) != 0) {
        ::close(sv[0]);
        ::close(sv[1]);
        throw std::runtime_error("sandbox: stderr pipe failed");
    }

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
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);  // no orphans if the parent dies
        ::dup2(sv[1], 0);
        ::dup2(sv[1], 1);
        ::dup2(err_pipe[1], 2);
        ::close(sv[0]);
        ::close(sv[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        ::execl(_worker_path.c_str(), "besq-worker", "--plugin", _plugin_path.c_str(),
                (const char *)nullptr);
        ::_exit(127);
    }
    ::close(sv[1]);
    ::close(err_pipe[1]);
    _fd = sv[0];
    _stderr_fd = err_pipe[0];
    _pid = static_cast<long>(pid);
#elif defined(_WIN32)
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};  // inheritable

    HANDLE to_child_rd = nullptr, to_child_wr = nullptr;    // parent→child (child stdin)
    HANDLE from_child_rd = nullptr, from_child_wr = nullptr; // child→parent (child stdout)
    HANDLE err_rd = nullptr, err_wr = nullptr;              // child stderr
    if (!::CreatePipe(&to_child_rd, &to_child_wr, &sa, 0) ||
        !::CreatePipe(&from_child_rd, &from_child_wr, &sa, 0) ||
        !::CreatePipe(&err_rd, &err_wr, &sa, 0))
        throw std::runtime_error("sandbox: CreatePipe failed");
    // Parent's ends must not be inherited by the child.
    ::SetHandleInformation(to_child_wr, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(from_child_rd, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = to_child_rd;
    si.hStdOutput = from_child_wr;
    si.hStdError  = err_wr;

    // Quote both paths (may contain spaces).
    const std::string cmdline = "\"" + _worker_path + "\" --plugin \"" + _plugin_path + "\"";
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessA(nullptr, const_cast<char *>(cmdline.c_str()), nullptr, nullptr,
                          TRUE /*inherit handles*/, 0, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(to_child_rd); ::CloseHandle(to_child_wr);
        ::CloseHandle(from_child_rd); ::CloseHandle(from_child_wr);
        ::CloseHandle(err_rd); ::CloseHandle(err_wr);
        throw std::runtime_error("sandbox: CreateProcess failed");
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
        jeli.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
            JOB_OBJECT_LIMIT_PROCESS_MEMORY |
            JOB_OBJECT_LIMIT_JOB_MEMORY;
        jeli.BasicLimitInformation.ActiveProcessLimit = 1;
        jeli.ProcessMemoryLimit = 512u * 1024 * 1024;  // 512 MB
        jeli.JobMemoryLimit     = 512u * 1024 * 1024;
        if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                       &jeli, sizeof(jeli)))
            LOG_WARN("sandbox: SetInformationJobObject failed — worker has no resource limits");
        if (!::AssignProcessToJobObject(job, pi.hProcess))
            LOG_WARN("sandbox: AssignProcessToJobObject failed — worker NOT in Job Object");
    }

    _fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(from_child_rd), _O_RDONLY | _O_BINARY);
    _write_fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(to_child_wr), _O_WRONLY | _O_BINARY);
    _stderr_handle = err_rd;  // raw handle for PeekNamedPipe-based stderr drain
    _pid = static_cast<long>(pi.dwProcessId);
    _proc_handle = pi.hProcess;
    _job_handle = job;
#else
    (void)0;
    throw std::runtime_error("sandbox: subprocess isolation not supported on this platform");
#endif
}

std::string SandboxedAlgorithm::take_worker_stderr() {
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
            break;  // nothing buffered / error
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
    while (_stderr_handle && ::PeekNamedPipe(_stderr_handle, nullptr, 0, nullptr, &avail, nullptr)
           && avail > 0) {
        DWORD got = 0;
        if (!::ReadFile(_stderr_handle, buf, static_cast<DWORD>(std::min<size_t>(sizeof(buf), avail)),
                        &got, nullptr) || got == 0)
            break;
        out.append(buf, static_cast<size_t>(got));
    }
    return out;
#else
    return {};
#endif
}

void SandboxedAlgorithm::query_metadata() {
    const char *reqs[] = {"MsgGetName", "MsgGetVersion", "MsgGetMode"};
    (void)reqs;  // placeholder — replaced below

    ipc::MsgType type;
    std::vector<uint8_t> result;

    send(ipc::MsgType::MsgGetName, {});
    if (!recv(type, result))
        throw std::runtime_error("sandbox: worker get_name failed");
    { ByteStreamReader r(result); r >> _name; }

    send(ipc::MsgType::MsgGetVersion, {});
    if (!recv(type, result))
        throw std::runtime_error("sandbox: worker get_version failed");
    { ByteStreamReader r(result); r >> _version; }

    send(ipc::MsgType::MsgGetMode, {});
    if (!recv(type, result))
        throw std::runtime_error("sandbox: worker get_mode failed");
    { ByteStreamReader r(result); uint8_t m = 0; r >> m; _mode = static_cast<AlgorithmMode>(m); }
}

void SandboxedAlgorithm::send(ipc::MsgType type, const std::vector<uint8_t> &payload) const {
    // Windows: writes go to the parent→child pipe; Linux uses the full-duplex
    // socketpair.
    const int fd = _write_fd >= 0 ? _write_fd : _fd;
    if (!ipc::write_frame(fd, type, payload))
        throw std::runtime_error("sandbox: worker IPC write failed");
}

bool SandboxedAlgorithm::recv(ipc::MsgType &type, std::vector<uint8_t> &payload) const {
    return ipc::read_frame(_fd, type, payload);
}

} // namespace algorithm
