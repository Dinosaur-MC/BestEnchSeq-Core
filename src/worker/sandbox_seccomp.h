#pragma once

/// @file worker/sandbox_seccomp.h
/// Linux seccomp-bpf sandbox for the besq-worker process.
///
/// Installed AFTER dlopen (the plugin's .so is already mapped), so the
/// policy does not need to allow the dynamic loader's open/mmap(PROT_EXEC).
///
/// Strategy (pragmatic for a compute sandbox):
///   - BLOCK file / network / process / debug syscalls with EPERM, so the
///     plugin's attempted I/O fails gracefully instead of SIGSYS-killing the
///     whole worker (its own Logger thread writes to files and must not crash).
///   - KILL mprotect(PROT_EXEC) — JIT / self-modifying code is never valid.
///   - Everything else is allowed (clone/clone3 for std::thread — a forked
///     child inherits this filter, and execve is blocked anyway).

/// Install the seccomp filter on the calling process (Linux only).
/// Returns true on success.  No-op (returns true) on non-Linux.
bool install_worker_seccomp();
