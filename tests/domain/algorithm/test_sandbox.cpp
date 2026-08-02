/// @file test_sandbox.cpp
/// Sandbox isolation end-to-end test (Linux).
///
/// Spawns a besq-worker via SandboxedAlgorithm with the malicious test
/// plugin (whose constructor tries open("/etc/passwd") — it runs AFTER
/// seccomp in the worker).  Asserts:
///   - the worker survives seccomp (native syscalls are allowed) and
///     responds to metadata queries
///   - the plugin's open() was blocked → its stderr report is "OPEN BLOCKED"
///
/// The plugin is built by the plugins/ tree.  Its path comes from
/// $BESQ_TEST_MALICIOUS_PLUGIN, or a default relative to the working dir.
/// If no plugin binary is present, the test SKIPS (returns 0) rather than
/// failing — the fixture is optional.

#include "framework/test_utils.h"
#include "domain/algorithm/sandbox/SandboxedAlgorithm.h"
#include "domain/algorithm/plugin/PluginAPI.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#pragma warning(disable : 4996)
#endif

using namespace algorithm;

namespace {

std::string find_plugin() {
    if (const char *env = std::getenv("BESQ_TEST_MALICIOUS_PLUGIN"); env && *env)
        if (std::filesystem::exists(env))
            return env;
    // Defaults relative to the CTest working directory (project root).
    const char *candidates[] = {
        "build-wsl/plugins/libalgo_malicious.so",
        "build/plugins/libalgo_malicious.so",
    };
    for (const char *c : candidates)
        if (std::filesystem::exists(c))
            return c;
    return {};
}

std::string find_worker() {
    if (const char *env = std::getenv("BESQ_WORKER_PATH"); env && *env)
        return env;
    const char *candidates[] = {
        "build-wsl/bin/besq-worker",
        "build/bin/besq-worker",
    };
    for (const char *c : candidates)
        if (std::filesystem::exists(c))
            return c;
    return "besq-worker";  // rely on PATH as a last resort
}

} // anonymous namespace

int main() {
#if !defined(__linux__)
    std::cout << "SKIP: test_sandbox is Linux-only (seccomp)" << std::endl;
    return 0;
#endif

    const std::string plugin = find_plugin();
    if (plugin.empty()) {
        std::cout << "SKIP: malicious test plugin not built (set BESQ_TEST_MALICIOUS_PLUGIN)"
                  << std::endl;
        return 0;
    }

    try {
        // ── Spawn the worker with the malicious plugin ─────────────────
        // The worker dlopens the plugin, installs seccomp, then constructs
        // the plugin (create_fn after seccomp) → its open() is EPERM'd.
        SandboxedAlgorithm sa(plugin, find_worker(), PluginCapability::None);

        // ── Worker survived seccomp + metadata query works ────────────
        expect(std::string(sa.name()) == "malicious", "sandbox: worker reports name");
        expect(std::string(sa.version()) == "1.0.0", "sandbox: worker reports version");

        // ── The plugin's file open was blocked by seccomp ─────────────
        const std::string stderr_out = sa.take_worker_stderr();
        expect(stderr_out.find("OPEN BLOCKED") != std::string::npos,
               "sandbox: plugin open() blocked (seccomp EPERM)");
        expect(stderr_out.find("OPEN OK") == std::string::npos,
               "sandbox: plugin must NOT have read the file");
    } catch (const test_error &e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return print_summary();
    } catch (const std::exception &e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return print_summary();
    }
    return print_summary();
}
