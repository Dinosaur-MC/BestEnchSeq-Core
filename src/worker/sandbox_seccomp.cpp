#include "sandbox_seccomp.h"

// Linux-only implementation.  On other platforms this is a no-op.
#if defined(__linux__)

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// ── BPF instruction helpers ──────────────────────────────────────────
sock_filter bpf_stmt(uint16_t code, uint32_t k) { return {code, 0, 0, k}; }
sock_filter bpf_jump(uint16_t code, uint8_t jt, uint8_t jf, uint32_t k) {
    return {code, jt, jf, k};
}
sock_filter bpf_ret(uint32_t k) {
    return bpf_stmt(BPF_RET | BPF_K, k);
}

/// LD | W | ABS — load a 32-bit word from the seccomp_data struct at
/// absolute offset \p off (the offset is the BPF `k` field).
sock_filter bpf_ld_abs(uint32_t off) {
    return {static_cast<uint16_t>(BPF_LD | BPF_W | BPF_ABS), 0, 0, off};
}

// ── Native audit arch (compile-time per-target) ──────────────────────
#if defined(__x86_64__)
constexpr uint32_t kAuditArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr uint32_t kAuditArch = AUDIT_ARCH_AARCH64;
#elif defined(__arm__)
constexpr uint32_t kAuditArch = AUDIT_ARCH_ARM;
#else
#error "besq-worker seccomp: unsupported architecture"
#endif

// ── Syscalls to block with EPERM (fail gracefully, not SIGSYS-kill) ──
// File / network / process / debug / privileged operations.  write() and
// read() are NOT blocked (already-open fds incl. the IPC channel and the
// Logger's file handles keep working); only creating/opening NEW ones is.
struct BlockedSyscalls {
    int numbers[24];
    int count;
};

const BlockedSyscalls kBlocked = [] {
    BlockedSyscalls b{};
    auto add = [&](long nr) { if (nr > 0) b.numbers[b.count++] = static_cast<int>(nr); };
    add(SYS_open);
    add(SYS_openat);
    add(SYS_creat);
    add(SYS_socket);
    add(SYS_connect);
    add(SYS_bind);
    add(SYS_listen);
    add(SYS_accept);
    add(SYS_accept4);
    add(SYS_sendto);
    add(SYS_recvfrom);
    add(SYS_sendmsg);
    add(SYS_recvmsg);
    add(SYS_execve);
    add(SYS_ptrace);
    add(SYS_process_vm_readv);
    add(SYS_process_vm_writev);
    add(SYS_mount);
    add(SYS_reboot);
    add(SYS_init_module);
    add(SYS_finit_module);
    add(SYS_delete_module);
#if defined(SYS_openat2)
    add(SYS_openat2);
#endif
#if defined(SYS_execveat)
    add(SYS_execveat);
#endif
    return b;
}();

// ── Build the BPF program ────────────────────────────────────────────
std::vector<sock_filter> build_filter() {
    std::vector<sock_filter> p;

    // 1. Architecture must be native.  BPF_JEQ: TRUE (native) jumps jt,
    //    FALSE (i386 int 0x80 compat) jumps jf.  jt=1, jf=0 → native skips
    //    the kill; non-native falls through to KILL.  (A prior jt=0,jf=1
    //    was inverted — it killed every native syscall and allowed the
    //    i386 compat path to escape the filter.)
    p.push_back(bpf_ld_abs(offsetof(struct seccomp_data, arch)));
    p.push_back(bpf_jump(BPF_JMP | BPF_JEQ | BPF_K, 1, 0, kAuditArch));
    p.push_back(bpf_ret(SECCOMP_RET_KILL_PROCESS));  // non-native → kill

    // 2. Load syscall number.
    p.push_back(bpf_ld_abs(offsetof(struct seccomp_data, nr)));

    // 3. Block list → EPERM (jeq nr, jt=0 → fall to ret, jf=1 → skip ret).
    for (int i = 0; i < kBlocked.count; ++i) {
        p.push_back(bpf_jump(BPF_JMP | BPF_JEQ | BPF_K, 0, 1,
                             static_cast<uint32_t>(kBlocked.numbers[i])));
        p.push_back(bpf_ret(SECCOMP_RET_ERRNO | EPERM));
    }

    // 4. mprotect: args[2] & PROT_EXEC → kill (JIT / self-modifying code).
    // Non-mprotect syscalls must skip ALL THREE following instructions —
    // otherwise the JSET below would test the syscall NUMBER (bits 0x4 set
    // on e.g. mmap=9, brk=12, clone=56) and kill legit syscalls.
    p.push_back(bpf_jump(BPF_JMP | BPF_JEQ | BPF_K, 0, 3,
                         static_cast<uint32_t>(SYS_mprotect)));
    p.push_back(bpf_ld_abs(offsetof(struct seccomp_data, args[2])));
    p.push_back(bpf_jump(BPF_JMP | BPF_JSET | BPF_K, 0, 1, PROT_EXEC));
    p.push_back(bpf_ret(SECCOMP_RET_KILL_PROCESS));  // PROT_EXEC → kill

    // 5. Everything else is allowed.
    p.push_back(bpf_ret(SECCOMP_RET_ALLOW));
    return p;
}

} // anonymous namespace

bool install_worker_seccomp() {
    // No new privileges: required before seccomp can be installed.
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return false;

    auto prog = build_filter();
    sock_fprog fprog{
        static_cast<unsigned short>(prog.size()),
        prog.data(),
    };

    // seccomp(SECCOMP_SET_MODE_FILTER, 0, &fprog)
    if (::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &fprog) != 0)
        return false;
    return true;
}

#else // !__linux__

bool install_worker_seccomp() { return true; }  // no-op

#endif
