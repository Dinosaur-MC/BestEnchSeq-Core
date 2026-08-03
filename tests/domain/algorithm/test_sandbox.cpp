/// @file test_sandbox.cpp
/// Sandbox isolation end-to-end test.
///
/// The sandbox seam is the EXECUTOR: a SandboxedExecutor runs a REAL
/// AlgorithmExecutor inside a besq-worker subprocess and mirrors its public
/// surface over coarse IPC.  This test drives SandboxedExecutor directly —
/// the same way the solve pipeline consumes it — and asserts:
///   - the worker survives seccomp (Linux) / binary IPC (Windows) and answers
///     metadata queries
///   - the malicious plugin's open("/etc/passwd") was blocked → its stderr
///     report is "OPEN BLOCKED" (Linux seccomp)
///   - pause/resume via the executor interface works end-to-end (no deadlock)
///   - a full checkpoint round-trip: solve 1 pauses + serializes; a FRESH
///     worker deserializes the opaque blob and resumes to completion
///
/// The plugins are built by the plugins/ tree; paths come from
/// $BESQ_TEST_MALICIOUS_PLUGIN / $BESQ_WORKER_PATH or build-tree defaults.
/// If no plugin binaries are present, the plugin-dependent tests SKIP.

#include "domain/algorithm/plugin/PluginAPI.h"
#include "domain/algorithm/sandbox/SandboxedExecutor.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/EnchSet.h"
#include "framework/test_utils.h"

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
    if (const char* env = std::getenv("BESQ_TEST_MALICIOUS_PLUGIN"); env && *env)
        if (std::filesystem::exists(env))
            return env;
    // Defaults relative to the CTest working directory (project root).
    // Platform-aware: build-wsl/ artifacts exist on Windows but are Linux .so
    // a Windows worker can't load — never pick them on Windows.
    const char* candidates[] = {
#if defined(_WIN32)
        "build/plugins/algo_malicious.dll",
#else
        "build-wsl/plugins/libalgo_malicious.so",
        "build/plugins/libalgo_malicious.so",
#endif
    };
    for (const char* c : candidates)
        if (std::filesystem::exists(c))
            return c;
    return {};
}

std::string find_worker() {
    if (const char* env = std::getenv("BESQ_WORKER_PATH"); env && *env)
        return env;
    // Platform-aware: build-wsl/ artifacts exist on Windows (readable) but are
    // Linux ELF binaries a Windows CreateProcess can't launch — prefer the
    // platform-native worker.  "build" is a native Windows build here; "build-wsl"
    // is the native Linux build on WSL.
    const char* candidates[] = {
#if defined(_WIN32)
        "build/bin/besq-worker.exe",
#else
        "build-wsl/bin/besq-worker",
        "build/bin/besq-worker",
#endif
    };
    for (const char* c : candidates)
        if (std::filesystem::exists(c))
            return c;
    return "besq-worker"; // rely on PATH as a last resort
}

/// A real search plugin for the pause/resume + checkpoint tests — the
/// malicious plugin's execute() returns instantly (can't pause) and idastar
/// has NO serializer, so we need AStar (serializable).  Returns an ABSOLUTE
/// path: the worker is a child process whose cwd may differ from the test's.
/// Platform-aware: build-wsl/ is present on Windows (readable files from a WSL
/// build) but those are Linux .so files a Windows worker can't load.
std::string find_search_plugin() {
    const char* candidates[] = {
#if defined(_WIN32)
        "build/plugins/algo_astar.dll",
#else
        "build-wsl/plugins/libalgo_astar.so",
        "build/plugins/libalgo_astar.so",
#endif
    };
    for (const char* c : candidates)
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
        infos[i].id = static_cast<uint8_t>(i);
        infos[i].mul = 1;
        infos[i].mul_b = 1;
        infos[i].applicable = true;
        infos[i].exc_mask = 0;
    }
    infos[0].max_lvl = 5; // sharpness
    infos[1].max_lvl = 5; // smite
    infos[2].max_lvl = 5; // bane_of_arthropods
    infos[3].max_lvl = 3; // knockback
    infos[4].max_lvl = 3; // looting
    infos[5].max_lvl = 3; // sweeping_edge
    infos[6].max_lvl = 3; // unbreaking
    infos[7].max_lvl = 2; // fire_aspect
    infos[8].max_lvl = 1; // mending
    infos[9].max_lvl = 1; // curse_of_vanishing
    // sharpness(0) / smite(1) / bane_of_arthropods(2) are mutually exclusive.
    infos[0].exc_mask = (mask_type{1} << 1) | (mask_type{1} << 2);
    infos[1].exc_mask = (mask_type{1} << 0) | (mask_type{1} << 2);
    infos[2].exc_mask = (mask_type{1} << 0) | (mask_type{1} << 1);

    Equipment equip;
    equip.id = NSID("minecraft:diamond_sword");
    equip.max_durability = 1561;
    equip.applicable_enchs = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    EnchReg reg;
    reg.init({infos[0], infos[1], infos[2], infos[3], infos[4], infos[5], infos[6], infos[7], infos[8], infos[9]},
             {NSID("minecraft:sharpness"), NSID("minecraft:smite"), NSID("minecraft:bane_of_arthropods"),
              NSID("minecraft:knockback"), NSID("minecraft:looting"), NSID("minecraft:sweeping_edge"),
              NSID("minecraft:unbreaking"), NSID("minecraft:fire_aspect"), NSID("minecraft:mending"),
              NSID("minecraft:curse_of_vanishing")},
             equip);

    AlgorithmInput input;
    input.registry = reg;
    input.config.mode = AlgorithmMode::direct;
    input.config.forge.platform = MCE::Java;
    EnchSet tgt;
    tgt.insert(0, 5);
    tgt.insert(3, 3);
    tgt.insert(4, 3);
    tgt.insert(5, 3);
    tgt.insert(6, 3);
    tgt.insert(7, 2);
    tgt.insert(8, 1);
    tgt.insert(9, 1);
    input.target = Item(ItemType::Equip, 1561, 0, tgt);
    input.data = DirectPayload{{Ench{0, 2}}};
    return input;
}

/// Pause/resume through the executor interface: start, pause mid-flight, wait,
/// resume — the solve must complete correctly (no deadlock, no corruption).
void test_pause_resume(const std::string& plugin) {
    auto input = build_search_input();
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);

    std::atomic<const char*> outcome{"?"};
    std::thread runner([&] {
        try {
            se.start(input);
            se.wait();
            outcome.store("completed", std::memory_order_release);
        } catch (const std::exception& e) {
            outcome.store(e.what(), std::memory_order_release);
        } catch (...) {
            outcome.store("threw-nonstd", std::memory_order_release);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    se.pause(); // parent → worker MsgPause → exec.pause()
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool paused_observed = se.state() == AlgorithmState::Paused;
    se.resume(); // parent → worker MsgResume → exec.resume()
    runner.join();

    std::string out(outcome.load());
    std::cout << "pause: outcome=" << out << " (paused observed=" << paused_observed << ")" << std::endl;
    expect(out == "completed", "sandbox: execute completed after pause/resume");
    expect(paused_observed, "sandbox: pause state observed during run");
    expect(se.state() == AlgorithmState::Completed, "sandbox: final state Completed");
}

/// Full checkpoint round-trip through the sandbox: solve 1 pauses and
/// serializes its state into an opaque blob; a FRESH worker deserializes the
/// blob and resumes to completion.  Exercises the executor seam + chunked IPC.
void test_checkpoint_roundtrip(const std::string& plugin) {
    auto input = build_search_input();

    // ── Solve 1: run, pause, serialize ──
    SandboxedExecutor se1(plugin, find_worker(), PluginCapability::None);
    se1.start(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // let the search start
    se1.pause();
    // Let the worker's executor actually reach its quiescent point (the
    // algorithm blocks in wait_if_paused) before snapshotting — same contract
    // as the in-process executor, no pipe-yield handshake.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto checkpoint = se1.serialize_state();
    std::cout << "checkpoint: " << checkpoint.size() << " bytes" << std::endl;
    expect(!checkpoint.empty(), "sandbox: checkpoint serialized while paused");

    se1.resume(); // let solve 1 finish cleanly
    se1.wait();

    // ── Solve 2: fresh worker, restore from the opaque checkpoint ──
    SandboxedExecutor se2(plugin, find_worker(), PluginCapability::None);
    se2.start(checkpoint);
    const auto state = se2.wait();
    const auto out = se2.output();
    std::cout << "resume: state=" << static_cast<int>(state) << " solutions=" << out.solutions.size() << std::endl;
    expect(state == AlgorithmState::Completed, "sandbox: resumed solve completed");
    expect(!out.solutions.empty(), "sandbox: resumed solve produced solutions");
}

/// Destroy a SandboxedExecutor MID-RUN without wait(): the destructor must
/// force the reader to exit and kill the worker promptly — no hang even if the
/// worker is busy (regression for review finding 2).
void test_destroy_mid_run(const std::string& plugin) {
    auto input = build_search_input();
    auto se = std::make_unique<SandboxedExecutor>(plugin, find_worker(), PluginCapability::None);
    se->start(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // search is running
    const auto t0 = std::chrono::steady_clock::now();
    se.reset(); // destructor: cancel → shutdown reader → join → kill worker
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "destroy-mid-run: " << dt << " ms" << std::endl;
    expect(dt < 5000, "sandbox: destroying mid-run returns promptly (no hang)");
}

/// Constructing with a bad plugin path throws, and the partially-constructed
/// executor must not leak the spawned worker (regression for review finding 3).
void test_bad_plugin_path() {
    bool threw = false;
    try {
        SandboxedExecutor se("nonexistent/libalgo_does_not_exist.so", find_worker(), PluginCapability::None);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "sandbox: bad plugin path throws (worker cleaned up on failure)");
}

/// One SandboxedExecutor reuses its worker across runs from Completed — the
/// worker hosts a real AlgorithmExecutor that allows re-running.
void test_reuse_after_completed(const std::string& plugin) {
    auto input = build_search_input();
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
    se.start(input);
    expect(se.wait() == AlgorithmState::Completed, "sandbox: first run completes");
    expect(!se.output().solutions.empty(), "sandbox: first run has solutions");
    se.start(input); // re-run on the SAME worker/executor (from Completed)
    expect(se.wait() == AlgorithmState::Completed, "sandbox: re-run completes");
    expect(!se.output().solutions.empty(), "sandbox: re-run has solutions");
}

/// Cancel racing an in-flight serialize handshake: the run-completion MsgResult
/// must never be mis-read as the checkpoint blob, and wait() must terminate
/// (regression for review findings 1/9).  Best-effort race — several iterations.
void test_cancel_races_serialize(const std::string& plugin) {
    for (int i = 0; i < 4; ++i) {
        auto input = build_search_input();
        SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
        se.start(input);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        se.pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // worker actually pauses

        std::vector<uint8_t> cp;
        std::thread serializer([&] { cp = se.serialize_state(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // handshake in flight
        se.cancel();                                               // now races the in-flight handshake
        serializer.join();
        const auto st = se.wait(); // must not hang
        std::cout << "cancel-race iter " << i << ": cp=" << cp.size() << " state=" << static_cast<int>(st) << std::endl;
        expect(st == AlgorithmState::Cancelled || st == AlgorithmState::Completed || st == AlgorithmState::Failed,
               "cancel-race: terminal state reached (wait did not hang)");
    }
}

/// Metadata/preflight through the executor surface: is_serializable,
/// supported_mode, simulate — differ between serializable (astar) and
/// non-serializable (malicious) plugins.
void test_metadata(const std::string& search_plugin, const std::string& malicious_plugin) {
    SandboxedExecutor astar(search_plugin, find_worker(), PluginCapability::None);
    expect(astar.is_serializable(), "sandbox: astar is serializable");
    expect(static_cast<int>(astar.supported_mode() & AlgorithmMode::direct) != 0, "sandbox: astar supports direct mode");
    expect(astar.simulate(build_search_input()), "sandbox: astar simulate reaches target");

    SandboxedExecutor mal(malicious_plugin, find_worker(), PluginCapability::None);
    expect(!mal.is_serializable(), "sandbox: malicious is not serializable");
}

/// serialize_state() is only valid while Paused — empty both before a run and
/// after it has completed (mirrors AlgorithmExecutor's contract).
void test_serialize_only_when_paused(const std::string& plugin) {
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
    expect(se.serialize_state().empty(), "sandbox: serialize before start returns {}");
    se.start(build_search_input());
    se.wait();
    expect(se.serialize_state().empty(), "sandbox: serialize after completion returns {}");
}

/// A garbage checkpoint blob must fail cleanly on the worker side (deserialize
/// throws → MsgError → Failed), never crash or hang.
void test_garbage_checkpoint_fails(const std::string& plugin) {
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
    const std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    se.start(garbage);
    const auto st = se.wait();
    std::cout << "garbage-checkpoint: state=" << static_cast<int>(st) << std::endl;
    expect(st == AlgorithmState::Failed, "sandbox: garbage checkpoint → Failed");
}

/// The malicious plugin's execute() returns instantly — the run completes with
/// an empty solution set (no hang, no crash).
void test_malicious_run(const std::string& plugin) {
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
    se.start(build_search_input()); // the plugin ignores the input
    const auto st = se.wait();
    expect(st == AlgorithmState::Completed, "sandbox: malicious execute completes");
    expect(se.output().solutions.empty(), "sandbox: malicious run has no solutions");
}

/// simulate()/evaluate() are pre-start only (fix 8): during a run they must
/// return the guarded default instead of touching the pipe (a second reader
/// would corrupt frames).
void test_preflight_during_run_guarded(const std::string& plugin) {
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
    auto input = build_search_input();
    se.start(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // still Running
    expect(!se.simulate(input), "sandbox: simulate during run returns false (guarded)");
    expect(se.evaluate(5) == 0.0, "sandbox: evaluate during run returns 0 (guarded)");
    se.wait();
}

/// Stress the pause/resume state machine: several mid-run cycles must leave the
/// final solve correct (no stuck pause, no deadlock).
void test_stress_pause_resume(const std::string& plugin) {
    auto input = build_search_input();
    SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);
    se.start(input);
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        se.pause();
        expect(se.state() == AlgorithmState::Paused, "sandbox: stress pause state");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        se.resume();
    }
    se.wait();
    expect(se.state() == AlgorithmState::Completed, "sandbox: stress run completes");
    expect(!se.output().solutions.empty(), "sandbox: stress run has solutions");
}

} // anonymous namespace

int main() {
    const std::string plugin = find_plugin();
    if (plugin.empty()) {
        std::cout << "SKIP: malicious test plugin not built (set BESQ_TEST_MALICIOUS_PLUGIN)" << std::endl;
        return 0;
    }

    try {
        // ── Spawn the worker with the malicious plugin ─────────────────
        // Linux: dlopen → seccomp → construct (open EPERM'd).  Windows:
        // CreateProcess + Job Object, no seccomp.
        SandboxedExecutor se(plugin, find_worker(), PluginCapability::None);

        // ── Worker survived + metadata query works ────────────────────
        expect(std::string(se.name()) == "malicious", "sandbox: worker reports name");
        expect(std::string(se.version()) == "1.0.0", "sandbox: worker reports version");

#if defined(__linux__)
        // ── seccomp isolation: the plugin's open("/etc/passwd") was EPERM'd ──
        const std::string stderr_out = se.take_worker_stderr();
        expect(stderr_out.find("OPEN BLOCKED") != std::string::npos, "sandbox: plugin open() blocked (seccomp EPERM)");
        expect(stderr_out.find("OPEN OK") == std::string::npos, "sandbox: plugin must NOT have read the file");
#else
        // ── Windows smoke test: binary IPC integrity ──────────────────
        // The request payload for evaluate(26) is the int16 bytes {0x1A, 0x00}.
        // A worker with CRT TEXT-mode stdin/stdout would mangle this (0x1A =
        // Ctrl-Z EOF) and fail to respond.  Verifies the _O_BINARY fix.
        std::cout << "SKIP seccomp assertion on Windows (no seccomp); "
                     "checking binary IPC with evaluate(26)..."
                  << std::endl;
        const double v = se.evaluate(26);
        expect(v >= 0.0, "sandbox: worker responded to evaluate(0x1A payload) — binary IPC ok");
#endif

        // ── Bad plugin path throws + cleans up the worker (fix 3) ──────
        test_bad_plugin_path();

        // ── Malicious plugin's execute runs to completion (no solutions) ──
        test_malicious_run(plugin);

        // ── Pause/resume + checkpoint + stress (needs a search plugin) ──
        const std::string search_plugin = find_search_plugin();
        if (search_plugin.empty()) {
            std::cout << "SKIP: pause/checkpoint/races tests — no search plugin built" << std::endl;
        } else {
            test_pause_resume(search_plugin);
            test_checkpoint_roundtrip(search_plugin);
            test_destroy_mid_run(search_plugin);        // fix 2
            test_reuse_after_completed(search_plugin);  // re-run from Completed
            test_cancel_races_serialize(search_plugin); // fix 1/9
            test_serialize_only_when_paused(search_plugin);
            test_garbage_checkpoint_fails(search_plugin);
            test_preflight_during_run_guarded(search_plugin);
            test_stress_pause_resume(search_plugin);
            test_metadata(search_plugin, plugin);
        }
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return print_summary();
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return print_summary();
    }
    return print_summary();
}
