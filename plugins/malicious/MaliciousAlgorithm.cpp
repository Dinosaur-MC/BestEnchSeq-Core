#include "MaliciousAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"

#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace algorithm {

MaliciousAlgorithm::MaliciousAlgorithm() noexcept {
#if defined(_WIN32)
    // Windows: try to read a real file with the Win32 API.  CreateFileA is a
    // RED audit symbol, so the cross-platform malicious plugin is flagged by
    // the binary audit on PE just like open() is on ELF.  Note: the Windows
    // sandbox (Job Object) does NOT block file access — that needs AppContainer
    // (M4) — so this OPENs OK in-process; only the audit gate stops it.
    HANDLE h = ::CreateFileA("C:\\Windows\\System32\\drivers\\etc\\hosts",
                             GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[80];
        DWORD n = 0;
        if (::ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr) && n > 0)
            buf[n] = '\0';
        else
            n = 0;
        ::CloseHandle(h);
        std::fprintf(stderr, "[malicious] OPEN OK: %zu bytes: %.60s\n",
                     static_cast<size_t>(n), buf);
    } else {
        std::fprintf(stderr, "[malicious] OPEN BLOCKED: error=%lu\n",
                     static_cast<unsigned long>(::GetLastError()));
    }
#elif defined(__linux__)
    // Linux: runs in the worker AFTER seccomp is installed → open() gets EPERM.
    // In-process it succeeds.  Report on stderr for the sandbox test.
    int fd = ::open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        char buf[80];
        ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
        if (n < 0)
            n = 0;
        buf[n] = '\0';
        ::close(fd);
        std::fprintf(stderr, "[malicious] OPEN OK: %zd bytes: %.60s\n", n, buf);
    } else {
        std::fprintf(stderr, "[malicious] OPEN BLOCKED: %s (errno=%d)\n",
                     std::strerror(errno), errno);
    }
#else
    (void)0;
    std::fprintf(stderr, "[malicious] no-op on this platform\n");
#endif
}

void MaliciousAlgorithm::execute(const AlgorithmInput & /*input*/, ExecutionContext &ctx) {
    ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
}

} // namespace algorithm
