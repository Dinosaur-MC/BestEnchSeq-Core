#define BESQ_TEST_MAIN
#include "domain/algorithm/IExecutor.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeStateSerializer.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "framework/test_framework.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
using namespace algorithm;

TEST_CASE("test_dp_merge_serializer_name") {
    DPMergeStateSerializer ser;
    expect(ser.algorithm_name() == "dp_merge", "algorithm_name should be dp_merge");
    expect(ser.algorithm_version() == "1.0.0", "algorithm_version should be 1.0.0");
    TEST_PASS("test_dp_merge_serializer_name");
}

TEST_CASE("test_dp_merge_serializer_interface") {
    auto ser = std::make_unique<DPMergeStateSerializer>();
    auto* base = dynamic_cast<IAlgorithmSerializer*>(ser.get());
    expect(base != nullptr, "DPMergeStateSerializer implements IAlgorithmSerializer");
    TEST_PASS("test_dp_merge_serializer_interface");
}

TEST_CASE("test_dp_merge_roundtrip") {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;

    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    EnchSet target_set;
    target_set.insert(Ench{0, 1});
    input.target.enchs = target_set;
    input.registry.init({}, {}, Equipment{});

    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    DPMergeAlgorithm algo2;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo2, out, blob);
    expect(ok, "round-trip deserialize should succeed");
    expect(out.target.type == ItemType::Equip, "target type should survive round-trip");
    TEST_PASS("test_dp_merge_roundtrip");
}

TEST_CASE("test_dp_merge_tamper_detected") {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;

    AlgorithmInput input;
    input.target.type = ItemType::Equip;
    input.registry.init({}, {}, Equipment{});
    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    if (blob.size() > 10) {
        blob[blob.size() - 5] ^= 0xFF;
        DPMergeAlgorithm algo2;
        AlgorithmInput out;
        bool ok = ser.deserialize(algo2, out, blob);
        expect(!ok, "tampered checkpoint should be rejected by CRC");
    }
    TEST_PASS("test_dp_merge_tamper_detected");
}

TEST_CASE("test_dp_merge_empty_rejected") {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo, out, std::span<const uint8_t>());
    expect(!ok, "empty checkpoint should return false");
    TEST_PASS("test_dp_merge_empty_rejected");
}

// ─── Populated-cache roundtrip ──────────────────────────────────────────
//
// The original serializer tests only roundtrip an EMPTY cache.  Since the memo
// cache is now bitmask-keyed (flat lock-free array for n ≤ 20), this test runs
// a real solve (populating the flat cache), serializes, restores into a fresh
// algorithm, and checks the restore is faithful by re-serializing: a correct
// restore must reproduce byte-identical state (serialization is deterministic
// — flat cache scan order and CRC are both content-derived).

namespace {

// Run a small direct-mode solve (base equipment + sharpness V + knockback II
// books → 3 items, all within the flat-cache range) so the memo cache has
// non-trivial content.  Returns the input for re-serializing the common
// section against the same state.
AlgorithmInput run_small_solve(DPMergeAlgorithm& algo) {
    std::vector<EnchInfo> infos(2);
    infos[0].id = 0;
    infos[0].mul = 1;
    infos[0].mul_b = 1;
    infos[0].max_lvl = 5;
    infos[0].exc_mask = 0;
    infos[0].applicable = true;
    infos[1].id = 1;
    infos[1].mul = 2;
    infos[1].mul_b = 1;
    infos[1].max_lvl = 2;
    infos[1].exc_mask = 0;
    infos[1].applicable = true;

    Equipment eq;
    eq.id = NSID("test");
    eq.max_durability = 1561;
    eq.applicable_enchs.insert(0);
    eq.applicable_enchs.insert(1);

    AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.config.mode = AlgorithmMode::direct;
    input.registry.init(std::move(infos), {NSID("sharpness"), NSID("knockback")}, eq);
    input.data = DirectPayload{}; // empty source → resolver generates all books
    input.target.type = ItemType::Equip;
    input.target.enchs.insert(Ench{0, 5});
    input.target.enchs.insert(Ench{1, 2});

    ExecutionContext ctx(0, "dp_merge");
    algo.init(input, ctx);
    algo.execute(input, ctx);
    return input;
}

} // anonymous namespace

TEST_CASE("test_dp_merge_populated_cache_roundtrip") {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;
    AlgorithmInput input = run_small_solve(algo);

    auto blob1 = ser.serialize(algo, input);
    expect(!blob1.empty(), "serialize populated cache should produce bytes");

    DPMergeAlgorithm algo2;
    AlgorithmInput out;
    bool ok = ser.deserialize(algo2, out, blob1);
    expect(ok, "populated-cache roundtrip should succeed");
    expect(out.target.type == ItemType::Equip, "target should survive roundtrip");

    // Faithful restore: re-serializing the restored algorithm must produce the
    // same bytes as the original.
    auto blob2 = ser.serialize(algo2, input);

    // The checkpoint MetaHeader embeds a wall-clock `timestamp` (bytes 12..19:
    // magic 4 + version 2 + flags 2 + num_sections 4, then int64 ms).  It is
    // pure metadata — excluded from the CRC by finalize() and never read back —
    // so the two serialize() calls may legitimately straddle a millisecond
    // boundary and differ by 1ms.  Zero it in both blobs so the comparison
    // asserts state fidelity rather than process timing.
    constexpr size_t kTimestampOffset = 4 + 2 + 2 + 4; // past magic/version/flags/num_sections
    constexpr size_t kTimestampSize = sizeof(int64_t); // MetaHeader::timestamp
    auto without_timestamp = [](std::vector<uint8_t> b) {
        for (size_t i = kTimestampOffset; i < kTimestampOffset + kTimestampSize && i < b.size(); ++i)
            b[i] = 0;
        return b;
    };
    expect(without_timestamp(blob1) == without_timestamp(blob2), "restore must be faithful (re-serialize identical)");
    TEST_PASS("test_dp_merge_populated_cache_roundtrip");
}

// ─── IExecutor::peek / extract_input / file roundtrip ────────────────────
//
// The resume flow (CLI --resume / GUI restore) inspects a checkpoint blob
// WITHOUT an executor: IExecutor::peek validates it and returns the stamped
// algorithm tag (used to create the executor), and
// IAlgorithmSerializer::extract_input reads the input section out for
// CompactAdapter::recall after start(checkpoint) has finished.

TEST_CASE("test_checkpoint_peek_and_file_roundtrip") {
    DPMergeStateSerializer ser;
    DPMergeAlgorithm algo;
    AlgorithmInput input = run_small_solve(algo);
    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "serialize should produce bytes");

    // peek: valid blob → algorithm_tag = "dp_merge".
    auto meta = IExecutor::peek(blob);
    expect(meta.algorithm_tag == "dp_merge", "peek returns the stamped algorithm tag");

    // extract_input: input section survives without an algorithm instance.
    AlgorithmInput extracted;
    expect(IAlgorithmSerializer::extract_input(blob, extracted), "extract_input succeeds");
    expect(extracted.target.type == input.target.type, "extracted target matches");

    // Invalid blobs: empty / garbage → empty tag / false.
    expect(IExecutor::peek(std::span<const uint8_t>()).algorithm_tag.empty(), "peek rejects empty blob");
    const std::vector<uint8_t> garbage{1, 2, 3, 4};
    expect(IExecutor::peek(garbage).algorithm_tag.empty(), "peek rejects garbage");
    AlgorithmInput out2;
    expect(!IAlgorithmSerializer::extract_input(garbage, out2), "extract_input rejects garbage");

    // File roundtrip: serialize → temp file → read back → peek again.
    const auto tmp = std::filesystem::temp_directory_path() / "besq_ckpt_roundtrip.ckpt";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        expect(static_cast<bool>(out), "checkpoint file written");
    }
    expect(std::filesystem::exists(tmp), "checkpoint file exists on disk");
    std::ifstream in(tmp, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> read_back(content.begin(), content.end());
    auto meta2 = IExecutor::peek(read_back);
    expect(meta2.algorithm_tag == "dp_merge", "peek on file bytes returns tag");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    TEST_PASS("test_checkpoint_peek_and_file_roundtrip");
}
