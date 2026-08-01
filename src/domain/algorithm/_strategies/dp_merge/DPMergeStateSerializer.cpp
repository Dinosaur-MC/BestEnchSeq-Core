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

    // The memo key is now a bitmask over the canonicalised `_base_items`, so
    // write the base items first to make the masks interpretable on restore.
    payload.u32(static_cast<uint32_t>(dp._base_items.size()));
    for (const auto& item : dp._base_items)
        payload << item;

    // Collect non-empty cache entries as (mask, frontier*) pairs: the flat
    // path scans the lock-free array; the map path iterates the fallback map.
    struct Entry { uint64_t mask; const DPMergeAlgorithm::Frontier* f; };
    std::vector<Entry> entries;
    if (dp._using_flat && dp._flat_cache) {
        for (size_t i = 0; i < dp._flat_capacity; ++i)
            if (const auto* f = dp._flat_cache[i].load(std::memory_order_relaxed))
                entries.push_back({static_cast<uint64_t>(i), f});
    } else {
        entries.reserve(dp._cache.size());
        for (const auto& [mask, f] : dp._cache)
            if (f) entries.push_back({mask, f.get()});
    }

    payload.u32(static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries) {
        payload.u64(e.mask);
        uint32_t entry_count = static_cast<uint32_t>(e.f->entries.size());
        payload.u32(entry_count);
        for (const auto& entry : e.f->entries) {
            payload.i64(entry.cost);
            payload.u8(entry.ppn);
            payload << entry.item;

            // Write steps (materialise the StepTree into the checkpoint)
            auto flat = entry.step_tree.materialize();
            uint32_t steps_n = static_cast<uint32_t>(flat.size());
            payload.u32(steps_n);
            for (const auto& step : flat) {
                payload << step.base << step.sacrifice << step.result;
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
    dp._owners.clear();

    // Restore the base items first (the cache masks index into these).
    uint32_t base_n = r.u32();
    if (base_n > MAX_SERIAL_ITEMS_PER_ENTRY) { r.set_fail(); return; }
    dp._base_items.clear();
    dp._base_items.reserve(base_n);
    for (uint32_t i = 0; i < base_n; ++i) {
        Item item;
        r >> item;
        if (!r.ok()) return;
        dp._base_items.push_back(std::move(item));
    }
    if (!r.ok()) return;

    // Reconstruct the cache storage sized to the base set.
    const size_t n = dp._base_items.size();
    if (n <= DPMergeAlgorithm::FLAT_CACHE_MAX_BITS) {
        dp._using_flat = true;
        const size_t slots = size_t{1} << n;
        dp._flat_cache = std::make_unique<std::atomic<DPMergeAlgorithm::Frontier*>[]>(slots);
        dp._flat_capacity = slots;
    } else {
        dp._using_flat = false;
        dp._flat_cache.reset();
        dp._flat_capacity = 0;
    }

    uint32_t count = r.u32();
    if (count > MAX_SERIAL_CACHE_ENTRIES) { r.set_fail(); return; }

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t mask = r.u64();
        DPMergeAlgorithm::Frontier frontier;
        uint32_t entry_count = r.u32();
        if (entry_count > MAX_SERIAL_FRONTIER_PER_ENTRY) { r.set_fail(); return; }
        frontier.entries.reserve(entry_count);

        for (uint32_t j = 0; j < entry_count; ++j) {
            DPMergeAlgorithm::ParetoEntry entry;
            entry.cost = r.i64();
            entry.ppn  = r.u8();
            r >> entry.item;
            if (!r.ok()) return;

            // Read steps and rebuild a linear StepTree
            uint32_t steps_n = r.u32();
            if (steps_n > MAX_SERIAL_STEPS_PER_ENTRY) { r.set_fail(); return; }
            std::vector<EnchStep> flat(steps_n);
            for (auto& step : flat) {
                r >> step.base >> step.sacrifice >> step.result;
                step.cost = r.i32();
                if (!r.ok()) return;
            }
            if (!r.ok()) return;
            entry.step_tree = StepTree::from_steps(flat);

            frontier.entries.push_back(std::move(entry));
        }
        if (!r.ok()) return;

        if (dp._using_flat) {
            // A corrupt/adversarial checkpoint could carry a mask ≥ 2^n — reject
            // it before the (heap) flat-array store goes out of bounds.
            if (mask >= dp._flat_capacity) { r.set_fail(); return; }
            // Publish into the flat array (owner kept alive in `_owners`).
            auto p = std::make_unique<DPMergeAlgorithm::Frontier>(std::move(frontier));
            dp._owners.push_back(std::move(p));
            dp._flat_cache[mask].store(dp._owners.back().get(), std::memory_order_relaxed);
        } else {
            dp._cache.emplace(mask,
                std::make_unique<DPMergeAlgorithm::Frontier>(std::move(frontier)));
        }
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
