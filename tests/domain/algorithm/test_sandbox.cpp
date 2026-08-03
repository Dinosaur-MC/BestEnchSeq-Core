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
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/sandbox/SandboxedAlgorithm.h"
#include "domain/algorithm/plugin/PluginAPI.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/EnchSet.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

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
#if defined(_WIN32)
        "build/bin/besq-worker.exe",
#endif
    };
    for (const char *c : candidates)
        if (std::filesystem::exists(c))
            return c;
    return "besq-worker";  // rely on PATH as a last resort
}

/// A real search plugin for the pause/resume + checkpoint tests — the
/// malicious plugin's execute() returns instantly (can't pause) and idastar
/// has NO serializer, so we need AStar (serializable).  Returns an ABSOLUTE
/// path: the worker is a child process whose cwd may differ from the test's.
/// Platform-aware: build-wsl/ is present on Windows (readable files from a WSL
/// build) but those are Linux .so files a Windows worker can't load.
std::string find_search_plugin() {
    const char *candidates[] = {
#if defined(_WIN32)
        "build/plugins/algo_astar.dll",
#else
        "build-wsl/plugins/libalgo_astar.so",
        "build/plugins/libalgo_astar.so",
#endif
    };
    for (const char *c : candidates)
        if (std::filesystem::exists(c))
            return std::filesystem::absolute(c).string();
    return {};
}

/// Diamond sword, 10-enchant registry (sharpness/smite/bane mutually
/// exclusive), 8 target enchants at high levels — a search that runs long
/// enough (hundreds of ms) to pause and checkpoint mid-run.  Smaller inputs
/// finished in ~100 ms, too fast to pause reliably.
AlgorithmInput build_search_input() {
    EnchInfo infos[10];
    for (int i = 0; i < 10; ++i) {
        infos[i].id         = static_cast<uint8_t>(i);
        infos[i].mul        = 1;
        infos[i].mul_b      = 1;
        infos[i].applicable = true;
        infos[i].exc_mask   = 0;
    }
    infos[0].max_lvl = 5;  // sharpness
    infos[1].max_lvl = 5;  // smite
    infos[2].max_lvl = 5;  // bane_of_arthropods
    infos[3].max_lvl = 3;  // knockback
    infos[4].max_lvl = 3;  // looting
    infos[5].max_lvl = 3;  // sweeping_edge
    infos[6].max_lvl = 3;  // unbreaking
    infos[7].max_lvl = 2;  // fire_aspect
    infos[8].max_lvl = 1;  // mending
    infos[9].max_lvl = 1;  // curse_of_vanishing
    // sharpness(0) / smite(1) / bane_of_arthropods(2) are mutually exclusive.
    infos[0].exc_mask = (mask_type{1} << 1) | (mask_type{1} << 2);
    infos[1].exc_mask = (mask_type{1} << 0) | (mask_type{1} << 2);
    infos[2].exc_mask = (mask_type{1} << 0) | (mask_type{1} << 1);

    Equipment equip;
    equip.id               = NSID("minecraft:diamond_sword");
    equip.max_durability   = 1561;
    equip.applicable_enchs = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    EnchReg reg;
    reg.init({infos[0], infos[1], infos[2], infos[3], infos[4], infos[5],
              infos[6], infos[7], infos[8], infos[9]},
             {NSID("minecraft:sharpness"), NSID("minecraft:smite"),
              NSID("minecraft:bane_of_arthropods"), NSID("minecraft:knockback"),
              NSID("minecraft:looting"), NSID("minecraft:sweeping_edge"),
              NSID("minecraft:unbreaking"), NSID("minecraft:fire_aspect"),
              NSID("minecraft:mending"), NSID("minecraft:curse_of_vanishing")},
             equip);

    AlgorithmInput input;
    input.registry          = reg;
    input.config.mode       = AlgorithmMode::direct;
    input.config.forge.platform = MCE::Java;
    EnchSet tgt;
    tgt.insert(0, 5); tgt.insert(3, 3); tgt.insert(4, 3); tgt.insert(5, 3);
    tgt.insert(6, 3); tgt.insert(7, 2); tgt.insert(8, 1); tgt.insert(9, 1);
    input.target = Item(ItemType::Equip, 1561, 0, tgt);
    input.data   = DirectPayload{{Ench{0, 2}}};
    return input;
}

/// Pause/resume forwarding: run a sandboxed execute, pause mid-flight, wait,
/// resume — the solve must complete correctly (no deadlock, no corruption).
void test_pause_resume(const std::string &plugin) {
    auto input = build_search_input();
    SandboxedAlgorithm sa(plugin, find_worker(), PluginCapability::None);

    ExecutionContext ctx(0, "pause-test");
    std::atomic<const char *> outcome{"?"};
    std::thread runner([&] {
        try {
            sa.execute(input, ctx);
            outcome.store("completed", std::memory_order_release);
        } catch (const std::exception &e) {
            outcome.store(e.what(), std::memory_order_release);
        } catch (...) {
            outcome.store("threw-nonstd", std::memory_order_release);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ctx.pause();                 // parent → worker MsgPause
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool paused_stall = ctx.is_paused();
    ctx.resume();                // parent → worker MsgResume
    runner.join();

    std::string out(outcome.load());
    std::cout << "pause: outcome=" << out << " (paused observed="
              << paused_stall << ")" << std::endl;
    expect(out == "completed", "sandbox: execute completed after pause/resume");
    expect(paused_stall, "sandbox: pause state observed during run");
}

/// Full checkpoint round-trip through the sandbox: solve 1 pauses and
/// serializes its state; a FRESH worker deserializes the checkpoint and resumes
/// to completion.  Exercises the proxy serializer + IPC chunked transfer.
void test_checkpoint_roundtrip(const std::string &plugin) {
    auto input = build_search_input();

    // ── Solve 1: run, pause, serialize ──
    auto sa1 = std::make_unique<SandboxedAlgorithm>(plugin, find_worker(), PluginCapability::None);
    AlgorithmExecutor exec1(std::move(sa1));
    exec1.start(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // let the search start
    exec1.pause();
    // serialize_state() spins until the execute loop has yielded the pipe.
    const auto checkpoint = exec1.serialize_state();
    std::cout << "checkpoint: " << checkpoint.size() << " bytes" << std::endl;
    expect(!checkpoint.empty(), "sandbox: checkpoint serialized while paused");

    exec1.resume();  // let solve 1 finish cleanly
    exec1.wait();

    // ── Solve 2: fresh worker, restore from the checkpoint ──
    auto sa2 = std::make_unique<SandboxedAlgorithm>(plugin, find_worker(), PluginCapability::None);
    AlgorithmExecutor exec2(std::move(sa2));
    exec2.start(checkpoint);
    const auto state = exec2.wait();
    const auto out = exec2.output();
    std::cout << "resume: state=" << static_cast<int>(state)
              << " solutions=" << out.solutions.size() << std::endl;
    expect(state == AlgorithmState::Completed, "sandbox: resumed solve completed");
    expect(!out.solutions.empty(), "sandbox: resumed solve produced solutions");
}

} // anonymous namespace

int main() {
    const std::string plugin = find_plugin();
    if (plugin.empty()) {
        std::cout << "SKIP: malicious test plugin not built (set BESQ_TEST_MALICIOUS_PLUGIN)"
                  << std::endl;
        return 0;
    }

    try {
        // ── Spawn the worker with the malicious plugin ─────────────────
        // Linux: dlopen → seccomp → construct (open EPERM'd).  Windows:
        // CreateProcess + Job Object, no seccomp.
        SandboxedAlgorithm sa(plugin, find_worker(), PluginCapability::None);

        // ── Worker survived + metadata query works ────────────────────
        expect(std::string(sa.name()) == "malicious", "sandbox: worker reports name");
        expect(std::string(sa.version()) == "1.0.0", "sandbox: worker reports version");

#if defined(__linux__)
        // ── seccomp isolation: the plugin's open("/etc/passwd") was EPERM'd ──
        const std::string stderr_out = sa.take_worker_stderr();
        expect(stderr_out.find("OPEN BLOCKED") != std::string::npos,
               "sandbox: plugin open() blocked (seccomp EPERM)");
        expect(stderr_out.find("OPEN OK") == std::string::npos,
               "sandbox: plugin must NOT have read the file");
#else
        // ── Windows smoke test: binary IPC integrity ──────────────────
        // The request payload for evaluate(26) is the int16 bytes {0x1A, 0x00}.
        // A worker with CRT TEXT-mode stdin/stdout would mangle this (0x1A =
        // Ctrl-Z EOF) and fail to respond.  Verifies the _O_BINARY fix.
        std::cout << "SKIP seccomp assertion on Windows (no seccomp); "
                     "checking binary IPC with evaluate(26)..." << std::endl;
        const double v = sa.evaluate(26);
        expect(v >= 0.0, "sandbox: worker responded to evaluate(0x1A payload) — binary IPC ok");
#endif

        // ── Pause/resume + checkpoint round-trip (needs a real search plugin) ──
        const std::string search_plugin = find_search_plugin();
        if (search_plugin.empty()) {
            std::cout << "SKIP: pause/checkpoint tests — no search plugin built" << std::endl;
        } else {
            test_pause_resume(search_plugin);
            test_checkpoint_roundtrip(search_plugin);
        }
    } catch (const test_error &e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return print_summary();
    } catch (const std::exception &e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return print_summary();
    }
    return print_summary();
}
