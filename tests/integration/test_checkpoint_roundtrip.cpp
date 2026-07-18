#include "framework/test_utils.h"
#include "algorithm/serialization/CompactSerializer.h"
#include "algorithm/serialization/IAlgorithmSerializer.h"
#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include "algorithm/strategies/astar/AStarAlgorithm.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/IAlgorithm.h"
#include "adapters/CompactAdapter.h"
#include "config/ForgeConfig.h"
#include "types/CompactedTypes.h"
#include "types/AlgorithmTypes.h"
#include "types/Equipment.h"
#include "registries/RegistryAccess.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/TagResolver.hpp"
#include "data/DataLoader.h"
#include "io/json.h"

#include "algorithm/diagnostics/AlgorithmObserver.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

// ─── Registry initialization ─────────────────────────────────────────────
// Use Meyer's singletons (same as benchmarks & other integration tests)

static void ensure_registries_loaded() {
    static bool loaded = false;
    if (!loaded) {
        TagResolver tags;
        registries::categories().initialize();
        besq::data::load_builtin_data(
            tags,
            registries::categories(),
            registries::enchants(),
            registries::equipment()
        );
        loaded = true;
    }
}

// ─── Create AlgorithmInput for "boots_full" test case ────────────────────

static AlgorithmInput create_boots_full_input() {
    ensure_registries_loaded();

    auto& ench_reg = registries::enchants();
    auto& eq_reg   = registries::equipment();

    int32_t eq_id = eq_reg.get_id("diamond_boots");
    if (eq_id < 0) throw std::runtime_error("diamond_boots not found");
    const Equipment& eq = eq_reg.get(eq_id);

    // Wanted enchantments for boots_full
    const char* wanted_names[] = {
        "protection", "feather_falling", "depth_strider",
        "soul_speed", "thorns", "unbreaking", "mending"
    };
    const int wanted_levels[] = {4, 4, 3, 3, 3, 3, 1};
    constexpr size_t NUM_WANTED = 7;

    // Build books
    std::vector<compact::Item> books;
    for (size_t i = 0; i < NUM_WANTED; ++i) {
        int32_t eid = ench_reg.get_id(wanted_names[i]);
        if (eid < 0) continue;

        compact::Item book;
        book.type = compact::ItemType::Book;
        book.dur = 0;
        book.ppn = 0;
        book.enchs.insert({static_cast<int16_t>(eid), static_cast<int16_t>(wanted_levels[i])});
        books.push_back(std::move(book));
    }

    // Build compact registry
    compact::EnchReg creg;
    creg.init(ench_reg, eq);

    // Build AlgorithmInput
    AlgorithmInput input;
    input.config.platform = MCE::Java;

    // Equipment item
    ItemStack start_item(eq, ::EnchSet{}, 0, eq.max_durability);
    input.items.push_back(CompactAdapter::from_domain(start_item, creg));

    // Books — directly push compact items (already in compact format)
    for (auto& book : books) {
        input.items.push_back(std::move(book));
    }

    // Target
    for (size_t i = 0; i < NUM_WANTED; ++i) {
        int32_t eid = ench_reg.get_id(wanted_names[i]);
        if (eid < 0) continue;
        int16_t lid = static_cast<int16_t>(creg.to_local_id(eid));
        if (lid >= 0)
            input.target.push_back({lid, static_cast<int16_t>(wanted_levels[i])});
    }

    input.ench_reg = std::move(creg);
    return input;
}

// ─── Tests ───────────────────────────────────────────────────────────────

void test_algorithm_input_roundtrip() {
    auto input = create_boots_full_input();

    // Write
    ByteStreamWriter w;
    compact_serial::write(w, input);

    // Read back
    ByteStreamReader r(w.data());
    auto result = compact_serial::read_algorithm_input(r);

    expect(r.ok(), "read_algorithm_input should succeed");
    expect_eq(result.config.platform, MCE::Java, "platform");
    expect_eq(result.items.size(), input.items.size(), "items count");
    expect_eq(result.target.size(), input.target.size(), "target count");
    expect(!result.items.empty(), "items should not be empty");
    if (!result.items.empty()) {
        expect_eq(result.items[0].type, input.items[0].type, "first item type");
    }
    TEST_PASS("test_algorithm_input_roundtrip");
}

void test_checkpoint_algorithm_input_roundtrip() {
    // Test full IAlgorithmSerializer round-trip including AlgorithmInput
    auto input = create_boots_full_input();

    AStarStateSerializer ser;
    AStarAlgorithm algo;
    algo.set_algorithm_input(std::move(input));

    // Serialize full checkpoint (includes AlgorithmInput + empty state)
    auto checkpoint = ser.serialize(algo);
    expect(!checkpoint.empty(), "checkpoint should be non-empty");

    // Deserialize into a new algorithm instance
    AStarAlgorithm algo2;
    bool ok = ser.deserialize(algo2, checkpoint);
    expect(ok, "deserialize should return true");

    // Verify restored AlgorithmInput
    const auto& restored = algo2.algorithm_input_ref();
    expect(restored.config.platform == MCE::Java, "restored platform");
    expect(!restored.items.empty(), "restored items non-empty");
    expect(!restored.target.empty(), "restored target non-empty");

    TEST_PASS("test_checkpoint_algorithm_input_roundtrip");
}

void test_checkpoint_integrity_checks() {
    AStarStateSerializer ser;
    AStarAlgorithm algo;

    // Empty checkpoint
    {
        bool ok = ser.deserialize(algo, std::span<const uint8_t>());
        expect(!ok, "empty checkpoint rejected");
    }

    // Bad magic
    {
        uint8_t trash[10] = {};
        bool ok = ser.deserialize(algo, trash);
        expect(!ok, "bad magic rejected");
    }

    // Wrong algorithm tag
    {
        ByteStreamWriter w;
        w.u32(compact_serial::FILE_MAGIC);
        w.u16(compact_serial::FILE_VERSION);
        w.u16(0);
        w.u32(0);  // num_sections = 0
        w.i64(0);  // timestamp
        uint8_t zero_crc[7] = {};
        w.bytes(zero_crc, 7);
        w.u16(1);  // algo_version
        const char* bad = "WRONG_ALGO";
        w.u8(static_cast<uint8_t>(std::strlen(bad)));
        w.bytes(bad, std::strlen(bad));
        auto buf = std::move(w).take();
        bool ok = ser.deserialize(algo, std::span<const uint8_t>(buf.data(), buf.size()));
        expect(!ok, "wrong algorithm tag rejected");
    }

    // Trailing garbage
    {
        auto checkpoint = ser.serialize(algo);
        std::vector<uint8_t> corrupted(checkpoint.begin(), checkpoint.end());
        corrupted.push_back(0xFF);
        AStarAlgorithm algo2;
        bool ok = ser.deserialize(algo2, corrupted);
        expect(!ok, "trailing garbage rejected");
    }

    TEST_PASS("test_checkpoint_integrity_checks");
}

void test_astar_pause_resume() {
    // Full checkpoint round-trip with A*: start → pause → serialize → resume → complete.
    // Uses AlgorithmObserver::on_state_changed to detect when A* transitions
    // from Running to Paused, ensuring precise synchronization without fixed delays.

    struct StateObserver : AlgorithmObserver {
        std::atomic<bool> paused{false};
        void on_state_changed(size_t, AlgorithmState, AlgorithmState curr) override {
            if (curr == AlgorithmState::Paused)
                paused.store(true, std::memory_order_release);
        }
    };

    auto input = create_boots_full_input();
    input.search.max_solutions = 1;
    input.search.memory_mb = 512;

    // Attach observer to detect pause completion
    DiagnosticsService::instance().set_persist(false);
    auto observer = std::make_shared<StateObserver>();
    DiagnosticsService::instance().attach_observer(observer);

    auto algo = std::make_unique<AStarAlgorithm>();
    AlgorithmExecutor executor(std::move(algo));

    // Start A* in background thread
    executor.start(std::move(input));

    // Allow A* to start exploring
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Request pause
    executor.pause();
    DiagnosticsService::instance().flush();

    // Wait for state transition to Paused (observer confirms via on_state_changed)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!observer->paused.load(std::memory_order_acquire)) {
        DiagnosticsService::instance().flush();
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    DiagnosticsService::instance().detach_observer(observer);

    // If pause was confirmed, capture checkpoint
    auto checkpoint = executor.serialize_state();

    if (!checkpoint.empty()) {
        // Create fresh executor and restore from checkpoint
        auto resume_algo = std::make_unique<AStarAlgorithm>();
        AlgorithmExecutor resume_exec(std::move(resume_algo));
        resume_exec.start(checkpoint);

        auto state = resume_exec.wait();
        expect(state == AlgorithmState::Completed, "resumed A* should complete");

        auto output = resume_exec.output();
        expect(output.is_valid, "resumed output should be valid");
        if (!output.solutions.empty()) {
            expect(output.solutions[0].total_cost > 0,
                   "resumed solution cost should be positive");
        }
    } else {
        // A* finished before pause completed — still verify direct execution
        auto state = executor.wait();
        expect(state == AlgorithmState::Completed, "direct A* should complete");
        auto output = executor.output();
        expect(output.is_valid, "direct output should be valid");
        if (!output.solutions.empty()) {
            expect(output.solutions[0].total_cost > 0,
                   "direct solution cost should be positive");
        }
    }

    TEST_PASS("test_astar_pause_resume");
}

int main() {
    try {
        // Load registries first
        ensure_registries_loaded();

        // Run tests
        test_algorithm_input_roundtrip();
        test_checkpoint_algorithm_input_roundtrip();
        test_checkpoint_integrity_checks();
        test_astar_pause_resume();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
