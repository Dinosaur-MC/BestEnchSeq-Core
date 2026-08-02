#include "domain/algorithm/sandbox/SandboxedAlgorithm.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "common/io/ByteStream.h"

#include <cstdlib>
#include <stdexcept>

// std::getenv is fine here (worker-path config); silence MSVC deprecation.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#pragma warning(disable : 4996)
#endif

#if defined(__linux__)
#include <csignal>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace algorithm {

namespace {

/// Env-var / default worker path resolution.
std::string resolve_worker_path(const std::string &given) {
    if (!given.empty())
        return given;
    if (const char *env = std::getenv("BESQ_WORKER_PATH"); env && *env)
        return env;
    return "besq-worker";
}

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
    if (_pid > 0) {
        // Kill the whole worker process group (covers plugin-forked children
        // that might keep the IPC channel open).
        ::kill(-_pid, SIGKILL);
        ::waitpid(_pid, nullptr, 0);
    }
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

    for (;;) {
        // Poll with a short timeout so we can observe parent-side cancellation.
        ipc::MsgType type;
        std::vector<uint8_t> payload;
        bool got = false;
#if defined(__linux__)
        struct pollfd pfd{_fd, POLLIN, 0};
        int rc = ::poll(&pfd, 1, 100);
        if (rc > 0)
            got = ipc::read_frame(_fd, type, payload);
        if (rc == 0 && ctx.is_cancelled()) {
            send(ipc::MsgType::MsgCancel, {});
            continue;
        }
        if (rc < 0)
            throw std::runtime_error("sandboxed execute: poll failed");
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
            return;
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

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(sv[0]);
        ::close(sv[1]);
        throw std::runtime_error("sandbox: fork failed");
    }
    if (pid == 0) {
        // Child: stdin/stdout are the IPC channel.  Become a process-group
        // leader so the parent can killpg() the whole group (worker + any
        // plugin-forked descendants) instead of just the direct child.
        ::setpgid(0, 0);
        // If the parent (besq) dies, take ourselves down too — no orphans.
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        ::dup2(sv[1], 0);
        ::dup2(sv[1], 1);
        ::close(sv[0]);
        ::close(sv[1]);
        ::execl(_worker_path.c_str(), "besq-worker", "--plugin", _plugin_path.c_str(),
                (const char *)nullptr);
        ::_exit(127);
    }
    ::close(sv[1]);
    _fd = sv[0];
    _pid = static_cast<long>(pid);
#else
    (void)0;
    throw std::runtime_error("sandbox: subprocess isolation is Linux-only in M1");
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
    if (!ipc::write_frame(_fd, type, payload))
        throw std::runtime_error("sandbox: worker IPC write failed");
}

bool SandboxedAlgorithm::recv(ipc::MsgType &type, std::vector<uint8_t> &payload) const {
    return ipc::read_frame(_fd, type, payload);
}

} // namespace algorithm
