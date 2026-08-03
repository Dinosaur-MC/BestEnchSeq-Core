// =============================================================================
// spawn_util.h — cross-platform spawn helper for system tests.
//
// Runs the besq CLI as a subprocess, capturing stdout/stderr into temp files
// and the process exit code.  The platform backends differ deliberately:
//
//   POSIX — popen + /bin/sh: the command line uses single-quote quoting (robust
//           against embedded quotes) with 1>/2> redirection and `echo $?` for
//           the exit code.
//   Windows — CreateProcessA directly (like SandboxedExecutor): stdout/stderr/
//           stdin are redirected via inheritable file handles, and the exit
//           code comes from GetExitCodeProcess.  We deliberately do NOT use
//           _popen: MSVCRT wraps the whole command in quotes for `cmd /c`,
//           and cmd's /c quote-stripping heuristics mangle any command that
//           itself contains quoted arguments (paths) → "syntax is incorrect".
//
// Env overrides are applied via a backup/restore around the spawn (the test is
// single-threaded, so mutating the parent env is safe).
// =============================================================================

#pragma once

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>  // _putenv_s, _dupenv_s, free
#include <windows.h>
#else
#include <cctype>    // std::isspace (POSIX rc-trim)
#include <sys/types.h>
#include <sys/wait.h>  // WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG
#include <unistd.h>    // getpid, popen, pclose
#endif

namespace besq_test {

struct RunResult {
    int exit_code = -1;  // -1 = spawn failed
    // NOTE: named `out`/`err` (not stdout/stderr) — those are stdio macros.
    std::string out;
    std::string err;
};

namespace detail {

// ── temp files (shared) ───────────────────────────────────────────────

inline std::filesystem::path temp_path(const char* tag, int seq) {
#if defined(_WIN32)
    const unsigned long pid = ::GetCurrentProcessId();
#else
    const long pid = static_cast<long>(::getpid());
#endif
    return std::filesystem::temp_directory_path()
         / ("besq_" + std::string(tag) + "_" + std::to_string(pid) + "_"
            + std::to_string(seq) + ".tmp");
}

struct TempFiles {
    std::filesystem::path out, err, in, rc;
    ~TempFiles() {
        std::error_code ec;
        std::filesystem::remove(out, ec);
        std::filesystem::remove(err, ec);
        std::filesystem::remove(rc, ec);
        std::filesystem::remove(in, ec);
    }
};

// Read a file in binary and normalize CRLF → LF (Windows text-mode output).
inline std::string read_file_norm(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out += s[i];
    }
    return out;
}

// ── POSIX backend (popen + /bin/sh) ────────────────────────────────────

#ifndef _WIN32

inline std::string sh_quote(const std::string& arg) {
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

inline std::string build_sh_cmd(const std::vector<std::string>& args,
                                const std::string& in_path,
                                const std::string& out_path,
                                const std::string& err_path,
                                const std::string& rc_path,
                                const std::vector<std::pair<std::string, std::string>>& extra_env) {
    std::string env;
    for (const auto& [k, v] : extra_env)
        env += k + "='" + v + "'; export " + k + "; ";

    std::string cmd = env;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd += " ";
        cmd += sh_quote(args[i]);
    }
    if (!in_path.empty()) cmd += " < " + sh_quote(in_path);
    cmd += " 1> " + sh_quote(out_path);
    cmd += " 2> " + sh_quote(err_path);
    cmd += "; echo $? > " + sh_quote(rc_path);
    return cmd;
}

inline RunResult run_cli_posix(const std::vector<std::string>& args,
                               const std::string& stdin_input,
                               const std::vector<std::pair<std::string, std::string>>& extra_env,
                               const TempFiles& tf) {
    const std::string cmd = build_sh_cmd(
        args, stdin_input.empty() ? std::string{} : tf.in.string(),
        tf.out.string(), tf.err.string(), tf.rc.string(), extra_env);

    RunResult r;
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return r;
    char buf[4096];
    while (::fgets(buf, sizeof(buf), p)) {}  // drain until EOF == shell exit
    int st = ::pclose(p);

    {  // rc.tmp authoritative; pclose fallback
        std::string rc = read_file_norm(tf.rc);
        while (!rc.empty() && std::isspace(static_cast<unsigned char>(rc.back())))
            rc.pop_back();
        if (!rc.empty()) {
            try { r.exit_code = std::stoi(rc); }
            catch (...) { r.exit_code = -1; }
        }
    }
    if (r.exit_code < 0 && st != -1) {
        r.exit_code = ::WIFEXITED(st)   ? ::WEXITSTATUS(st)
                    : ::WIFSIGNALED(st) ? 128 + ::WTERMSIG(st)
                                        : -1;
    }
    r.out = read_file_norm(tf.out);
    r.err = read_file_norm(tf.err);
    return r;
}

#endif  // !_WIN32

// ── Windows backend (CreateProcessA + handle redirection) ───────────────

#if defined(_WIN32)

// Quote a single argument for a CreateProcess command line (CommandLineToArgvW
// rules).  Args without spaces/tabs/quotes pass through unchanged.
inline std::string win_quote_arg(const std::string& arg) {
    const bool needs = arg.find_first_of(" \t\"") != std::string::npos;
    if (!needs) return arg;
    std::string out = "\"";
    int backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            out.append(static_cast<size_t>(backslashes) * 2 + 1, '\\');
            out += '"';
            backslashes = 0;
        } else {
            out.append(static_cast<size_t>(backslashes), '\\');
            backslashes = 0;
            out += c;
        }
    }
    if (backslashes) out.append(static_cast<size_t>(backslashes) * 2, '\\');
    out += '"';
    return out;
}

// RAII: set env overrides around a spawn, restore on scope exit.
class EnvGuard {
public:
    explicit EnvGuard(const std::vector<std::pair<std::string, std::string>>& extra) {
        saved_.reserve(extra.size());
        for (const auto& [k, v] : extra) {
            char* old = nullptr;
            size_t len = 0;
            std::string prev;
            if (::_dupenv_s(&old, &len, k.c_str()) == 0 && old) {
                prev.assign(old);
                ::free(old);
            }
            saved_.emplace_back(k, prev);
            ::_putenv_s(k.c_str(), v.c_str());
        }
    }
    ~EnvGuard() {
        for (size_t i = saved_.size(); i-- > 0;)
            _putenv_s(saved_[i].first.c_str(), saved_[i].second.c_str());
    }
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    std::vector<std::pair<std::string, std::string>> saved_;
};

inline RunResult run_cli_win(const std::vector<std::string>& args,
                             const std::string& stdin_input,
                             const std::vector<std::pair<std::string, std::string>>& extra_env,
                             const TempFiles& tf) {
    EnvGuard env_guard(extra_env);

    // Build the command line: <exe> <arg...>.
    std::string cmdline;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmdline += " ";
        cmdline += win_quote_arg(args[i]);
    }
    std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back('\0');

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};  // inheritable handles

    HANDLE h_out = ::CreateFileA(tf.out.string().c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE h_err = ::CreateFileA(tf.err.string().c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE h_in = nullptr;
    if (!stdin_input.empty()) {
        h_in = ::CreateFileA(tf.in.string().c_str(), GENERIC_READ,
                             FILE_SHARE_READ, &sa, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h_out;
    si.hStdError = h_err;
    si.hStdInput = h_in ? h_in : ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    RunResult r;
    const BOOL ok = ::CreateProcessA(
        nullptr, cmd_buf.data(), nullptr, nullptr, /*bInheritHandles=*/TRUE,
        0, nullptr, nullptr, &si, &pi);
    if (h_in) ::CloseHandle(h_in);
    if (!ok) {
        ::CloseHandle(h_out);
        ::CloseHandle(h_err);
        std::fprintf(stderr, "[spawn] CreateProcess failed (code %lu)\n",
                     static_cast<unsigned long>(::GetLastError()));
        return r;
    }

    ::CloseHandle(h_out);
    ::CloseHandle(h_err);
    ::WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<int>(code);

    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    r.out = read_file_norm(tf.out);
    r.err = read_file_norm(tf.err);
    return r;
}

#endif  // _WIN32

}  // namespace detail

// Run `besq <args...>` (args = argv[1..]) with optional stdin injection and
// extra environment overrides.  Returns captured stdout/stderr (LF-normalized)
// and the exit code (0/1 semantics; -1 if the spawn itself failed).
inline RunResult run_cli(
    const std::vector<std::string>& args,
    const std::string& stdin_input = {},
    const std::vector<std::pair<std::string, std::string>>& extra_env = {}) {
    static std::atomic<int> g_seq{0};
    const int seq = ++g_seq;

    detail::TempFiles tf{
        detail::temp_path("out", seq),
        detail::temp_path("err", seq),
        detail::temp_path("in", seq),
        detail::temp_path("rc", seq),
    };

    if (!stdin_input.empty()) {
        std::ofstream f(tf.in, std::ios::binary);
        f << stdin_input;
    }

#if defined(_WIN32)
    return detail::run_cli_win(args, stdin_input, extra_env, tf);
#else
    return detail::run_cli_posix(args, stdin_input, extra_env, tf);
#endif
}

}  // namespace besq_test
