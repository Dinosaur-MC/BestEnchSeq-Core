#include "MaliciousAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace algorithm {

MaliciousAlgorithm::MaliciousAlgorithm() noexcept {
#if defined(__linux__)
    // Runs in the worker AFTER seccomp is installed → open() gets EPERM.
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
    std::fprintf(stderr, "[malicious] no-op on this platform (sandbox is Linux)\n");
#endif
}

void MaliciousAlgorithm::execute(const AlgorithmInput & /*input*/, ExecutionContext &ctx) {
    ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
}

} // namespace algorithm
