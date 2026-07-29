#include "DPMergeStateSerializer.h"
#include "DPMergeAlgorithm.h"
#include "common/io/ByteStream.h"
#include <unordered_map>

namespace algorithm {

// ── DPMerge-specific section tag constants ─────────────────────────
namespace {
    constexpr uint32_t TAG_CACHE   = 1;
    constexpr uint32_t TAG_SCALARS = 2;

    // Hard upper bounds for deserialized counts (OOM/DoS protection)
    constexpr uint32_t MAX_SERIAL_CACHE_ENTRIES = 500'000;
    constexpr uint32_t MAX_SERIAL_FRONTIER_PER_ENTRY = 10'000;
    constexpr uint32_t MAX_SERIAL_STEPS_PER_ENTRY   = 10'000;
    constexpr uint32_t MAX_SERIAL_ITEMS_PER_ENTRY   = 256;
}

// ─── _serialize_state ─────────────────────────────────────────────────

std::vector<checkpoint::Section> DPMergeStateSerializer::_serialize_state(const IAlgorithm& algo) const {
    const auto& dp = static_cast<const DPMergeAlgorithm&>(algo);
    std::vector<checkpoint::Section> sections;
    sections.reserve(2);
    sections.push_back(_write_cache(dp));
    sections.push_back(_write_scalars(dp));
    return sections;
}

// ─── _deserialize_state ───────────────────────────────────────────────

bool DPMergeStateSerializer::_deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const {
    auto& dp = static_cast<DPMergeAlgorithm&>(algo);

    for (const auto& sect : sections) {
        auto tag = checkpoint::get_algo_tag(sect.header.type);
        ByteStreamReader r(sect.payload.data(), sect.payload.size());
        switch (tag) {
            case TAG_CACHE:   _read_cache(r, dp);   break;
            case TAG_SCALARS: _read_scalars(r, dp);  break;
            default:
                break;
        }
        if (!r.ok()) return false;
    }

    return true;
}

// ─── Write helpers ───────────────────────────────────────────────────

checkpoint::Section DPMergeStateSerializer::_write_cache(const DPMergeAlgorithm& dp) {
    ByteStreamWriter payload;
    uint32_t count = static_cast<uint32_t>(dp._cache.size());
    payload.u32(count);

    for (const auto& [key, frontier] : dp._cache) {
        // Write key (ItemCollection)
        payload.u32(static_cast<uint32_t>(key.size()));
        for (const auto& item : key)
            payload << item;

        // Write value (Frontier)
        uint32_t entry_count = static_cast<uint32_t>(frontier.entries.size());
        payload.u32(entry_count);
        for (const auto& entry : frontier.entries) {
            payload.i64(entry.cost);
            payload.u8(entry.ppn);
            payload << entry.item;

            // Write steps (materialise the StepTree into the checkpoint)
            auto flat = entry.step_tree.materialize();
            uint32_t steps_n = static_cast<uint32_t>(flat.size());
            payload.u32(steps_n);
            for (const auto& step : flat) {
                payload << step.base << step.sacrifice;
                payload.i32(step.cost);
            }
        }
    }

    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_CACHE);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

checkpoint::Section DPMergeStateSerializer::_write_scalars(const DPMergeAlgorithm& dp) {
    ByteStreamWriter payload;
    // Version marker for forward compatibility
    payload.u8(1);
    // Cache entry count (informational)
    payload.u32(static_cast<uint32_t>(dp._cache.size()));
    (void)dp;
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_SCALARS);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

// ─── Read helpers ─────────────────────────────────────────────────────

void DPMergeStateSerializer::_read_cache(ByteStreamReader& r, DPMergeAlgorithm& dp) const {
    dp._cache.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_CACHE_ENTRIES) { r.set_fail(); return; }

    for (uint32_t i = 0; i < count; ++i) {
        // Read key (ItemCollection)
        uint32_t key_size = r.u32();
        if (key_size > MAX_SERIAL_ITEMS_PER_ENTRY) { r.set_fail(); return; }
        ItemCollection key(key_size);
        for (auto& item : key) {
            r >> item;
            if (!r.ok()) break;
        }
        if (!r.ok()) break;

        // Read value (Frontier)
        DPMergeAlgorithm::Frontier frontier;
        uint32_t entry_count = r.u32();
        if (entry_count > MAX_SERIAL_FRONTIER_PER_ENTRY) { r.set_fail(); return; }
        frontier.entries.reserve(entry_count);

        for (uint32_t j = 0; j < entry_count; ++j) {
            DPMergeAlgorithm::ParetoEntry entry;
            entry.cost = r.i64();
            entry.ppn  = r.u8();
            r >> entry.item;
            if (!r.ok()) break;

            // Read steps and rebuild a linear StepTree
            uint32_t steps_n = r.u32();
            if (steps_n > MAX_SERIAL_STEPS_PER_ENTRY) { r.set_fail(); return; }
            std::vector<EnchStep> flat(steps_n);
            for (auto& step : flat) {
                r >> step.base >> step.sacrifice;
                step.cost = r.i32();
                if (!r.ok()) break;
            }
            if (!r.ok()) break;
            entry.step_tree = StepTree::from_flat(flat);

            frontier.entries.push_back(std::move(entry));
        }
        if (!r.ok()) break;

        dp._cache[std::move(key)] = std::move(frontier);
    }
}

void DPMergeStateSerializer::_read_scalars(ByteStreamReader& r, DPMergeAlgorithm& dp) const {
    // Version marker
    uint8_t version = r.u8();
    (void)version;
    // Informational cache count (not used for state restoration)
    uint32_t cache_count = r.u32();
    (void)cache_count;
    (void)dp;
}

} // namespace algorithm
