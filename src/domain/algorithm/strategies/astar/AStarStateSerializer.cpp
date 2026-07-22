#include "AStarStateSerializer.h"
#include "AStarAlgorithm.h"
#include "../../serialization/CompactSerializer.h"
namespace algorithm {

using namespace compact_serial;

// ── Section tag constants (algo-specific, meaningful only to this serializer) ──
namespace {
    constexpr uint32_t TAG_ITEM_POOL  = 1;
    constexpr uint32_t TAG_STEP_POOL  = 2;
    constexpr uint32_t TAG_OPEN_HEAP  = 3;
    constexpr uint32_t TAG_BEST_G     = 4;
    constexpr uint32_t TAG_SCALARS    = 5;
}

// ─── _serialize_state: bundle each internal structure as an opaque section ──

std::vector<AlgoSectionData> AStarStateSerializer::_serialize_state(const IAlgorithm& algo) const {
    const auto& astar = static_cast<const AStarAlgorithm&>(algo);
    std::vector<AlgoSectionData> sections;
    sections.reserve(5);
    sections.push_back(_write_item_pool(astar));
    sections.push_back(_write_step_pool(astar));
    sections.push_back(_write_open_heap(astar));
    sections.push_back(_write_best_g(astar));
    sections.push_back(_write_scalars(astar));
    return sections;
}

// ─── _deserialize_state: dispatch by section_tag ─────────────────────────

bool AStarStateSerializer::_deserialize_state(IAlgorithm& algo, std::span<const AlgoSectionData> sections) const {
    auto& astar = static_cast<AStarAlgorithm&>(algo);

    for (const auto& sect : sections) {
        ByteStreamReader r(sect.payload);
        switch (sect.section_tag) {
            case TAG_ITEM_POOL: _read_item_pool(r, astar); break;
            case TAG_STEP_POOL: _read_step_pool(r, astar); break;
            case TAG_OPEN_HEAP: _read_open_heap(r, astar); break;
            case TAG_BEST_G:    _read_best_g(r, astar);    break;
            case TAG_SCALARS:   _read_scalars(r, astar);   break;
            default:
                break;  // unknown tag — skip
        }
        if (!r.ok()) return false;
    }

    astar._deserialize_ok = true;
    astar._state_restored = true;
    return true;
}

// ─── Write helpers ───────────────────────────────────────────────────────

AlgoSectionData AStarStateSerializer::_write_item_pool(const AStarAlgorithm& astar) {
    uint32_t count = static_cast<uint32_t>(astar._pool.size());
    ByteStreamWriter payload;
    payload.u32(count);
    for (AStarAlgorithm::ItemID i = 0; static_cast<uint32_t>(i) < count; ++i)
        write(payload, astar._pool[i]);
    return {TAG_ITEM_POOL, std::move(payload).take()};
}

AlgoSectionData AStarStateSerializer::_write_step_pool(const AStarAlgorithm& astar) {
    uint32_t count = static_cast<uint32_t>(astar._step_pool.size());
    ByteStreamWriter payload;
    payload.u32(count);
    for (const auto& sn : astar._step_pool) {
        payload.i32(sn.prev);
        payload.i32(sn.base_id);
        payload.i32(sn.sac_id);
        payload.i32(sn.cost);
    }
    return {TAG_STEP_POOL, std::move(payload).take()};
}

AlgoSectionData AStarStateSerializer::_write_open_heap(const AStarAlgorithm& astar) {
    uint32_t count = static_cast<uint32_t>(astar._open_heap.size());
    ByteStreamWriter payload;
    payload.u32(count);
    for (const auto& entry : astar._open_heap) {
        const auto& state = entry.state;
        payload.i32(state.g);
        payload.i32(state.h);
        payload.u64(static_cast<uint64_t>(state.hash));
        payload.i32(state.step_idx);
        uint32_t ids_n = static_cast<uint32_t>(state.ids.size());
        payload.u32(ids_n);
        for (auto id : state.ids)
            payload.i32(id);
        payload.i32(entry.f);
    }
    return {TAG_OPEN_HEAP, std::move(payload).take()};
}

AlgoSectionData AStarStateSerializer::_write_best_g(const AStarAlgorithm& astar) {
    ByteStreamWriter payload;
    astar._x_export_best_g(payload);
    return {TAG_BEST_G, std::move(payload).take()};
}

AlgoSectionData AStarStateSerializer::_write_scalars(const AStarAlgorithm& astar) {
    ByteStreamWriter payload;
    payload.i32(astar._best_solution_cost);
    payload.i32(astar._solutions_found);
    payload.i64(astar._x_explored());
    return {TAG_SCALARS, std::move(payload).take()};
}

// ─── Read helpers (same as before, with count limits + ok() checks) ────

void AStarStateSerializer::_read_item_pool(ByteStreamReader& r, AStarAlgorithm& astar) const {
    astar._pool.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_ITEMS) { r.set_fail(); return; }
    for (uint32_t i = 0; i < count; ++i) {
        astar._pool.add(read_item(r));
        if (!r.ok()) break;
    }
}

void AStarStateSerializer::_read_step_pool(ByteStreamReader& r, AStarAlgorithm& astar) const {
    astar._step_pool.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_STEPS) { r.set_fail(); return; }
    astar._step_pool.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AStarAlgorithm::StepNode sn;
        sn.prev    = r.i32();
        sn.base_id = r.i32();
        sn.sac_id  = r.i32();
        sn.cost    = r.i32();
        astar._step_pool.push_back(std::move(sn));
        if (!r.ok()) break;
    }
}

void AStarStateSerializer::_read_open_heap(ByteStreamReader& r, AStarAlgorithm& astar) const {
    astar._open_heap.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_HEAP) { r.set_fail(); return; }
    astar._open_heap.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AStarAlgorithm::PriorityEntry entry;
        entry.state.g        = r.i32();
        entry.state.h        = r.i32();
        entry.state.hash     = static_cast<size_t>(r.u64());
        entry.state.step_idx = r.i32();
        uint32_t ids_n = r.u32();
        if (ids_n > MAX_SERIAL_ITEMS) { r.set_fail(); break; }
        entry.state.ids.reserve(ids_n);
        for (uint32_t j = 0; j < ids_n; ++j) {
            entry.state.ids.push_back(r.i32());
            if (!r.ok()) break;
        }
        entry.f = r.i32();
        if (!r.ok()) break;
        astar._open_heap.push_back(std::move(entry));
    }
}

void AStarStateSerializer::_read_best_g(ByteStreamReader& r, AStarAlgorithm& astar) const {
    astar._x_import_best_g(r);
}

void AStarStateSerializer::_read_scalars(ByteStreamReader& r, AStarAlgorithm& astar) const {
    astar._best_solution_cost = r.i32();
    astar._solutions_found    = r.i32();
    astar._x_set_explored(r.i64());
}

} // namespace algorithm
