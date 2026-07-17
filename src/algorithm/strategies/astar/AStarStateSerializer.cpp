#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include "algorithm/strategies/astar/AStarAlgorithm.h"
#include "algorithm/serialization/CompactSerializer.h"

using namespace compact_serial;

// ─── Algo section write ──────────────────────────────────────────────────

void AStarStateSerializer::_write_algo_sections(
    ByteStreamWriter& w, const IAlgorithm& algo, uint32_t& next_id) const
{
    const auto& astar = static_cast<const AStarAlgorithm&>(algo);
    _write_item_pool(w, astar, next_id);
    _write_step_pool(w, astar, next_id);
    _write_open_heap(w, astar, next_id);
    _write_best_g(w, astar, next_id);
    _write_scalars(w, astar, next_id);
}

// ─── Algo section read ───────────────────────────────────────────────────

void AStarStateSerializer::_read_algo_sections(
    ByteStreamReader& r, IAlgorithm& algo) const
{
    auto& astar = static_cast<AStarAlgorithm&>(algo);

    // Parse all sections from the stream: common sections are skipped,
    // algorithm sections are dispatched.
    while (r.has_more()) {
        auto si = read_section_header(r);
        if (si.type == 0 && si.section_id == 0)
            break;

        if ((si.type & 0x80000000u) == 0) {
            // Common section — skip payload
            r.skip(static_cast<size_t>(si.len));
            continue;
        }

        // Algorithm-specific section — route by order of appearance
        // (section_id not used for routing; sections are in write order)
        switch (si.section_id) {
            case 2: _read_item_pool(r, astar); break;
            case 3: _read_step_pool(r, astar); break;
            case 4: _read_open_heap(r, astar); break;
            case 5: _read_best_g(r, astar);    break;
            case 6: _read_scalars(r, astar);   break;
            default:
                r.skip(static_cast<size_t>(si.len));
                break;
        }
    }

    astar._state_restored = true;
}

// ─── Write helpers ───────────────────────────────────────────────────────

void AStarStateSerializer::_write_item_pool(
    ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const
{
    uint32_t count = static_cast<uint32_t>(astar._pool.size());
    ByteStreamWriter payload;
    payload.u32(count);
    for (AStarAlgorithm::ItemID i = 0; static_cast<uint32_t>(i) < count; ++i)
        write(payload, astar._pool[i]);

    auto p = std::move(payload).take();
    write_section_header(w, SECTION_TYPE_ALGO, sid++, p.size());
    w.bytes(p.data(), p.size());
}

void AStarStateSerializer::_write_step_pool(
    ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const
{
    uint32_t count = static_cast<uint32_t>(astar._step_pool.size());
    ByteStreamWriter payload;
    payload.u32(count);
    for (const auto& sn : astar._step_pool) {
        payload.i32(sn.prev);
        payload.i32(sn.base_id);
        payload.i32(sn.sac_id);
        payload.i32(sn.cost);
    }

    auto p = std::move(payload).take();
    write_section_header(w, SECTION_TYPE_ALGO, sid++, p.size());
    w.bytes(p.data(), p.size());
}

void AStarStateSerializer::_write_open_heap(
    ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const
{
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

    auto p = std::move(payload).take();
    write_section_header(w, SECTION_TYPE_ALGO, sid++, p.size());
    w.bytes(p.data(), p.size());
}

void AStarStateSerializer::_write_best_g(
    ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const
{
    ByteStreamWriter payload;
    astar._x_export_best_g(payload);

    auto p = std::move(payload).take();
    write_section_header(w, SECTION_TYPE_ALGO, sid++, p.size());
    w.bytes(p.data(), p.size());
}

void AStarStateSerializer::_write_scalars(
    ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const
{
    ByteStreamWriter payload;
    payload.i32(astar._best_solution_cost);
    payload.i32(astar._solutions_found);
    payload.i64(astar._x_explored());

    auto p = std::move(payload).take();
    write_section_header(w, SECTION_TYPE_ALGO, sid++, p.size());
    w.bytes(p.data(), p.size());
}

// ─── Read helpers ────────────────────────────────────────────────────────

void AStarStateSerializer::_read_item_pool(
    ByteStreamReader& r, AStarAlgorithm& astar) const
{
    astar._pool.clear();
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
        astar._pool.add(read_item(r));
}

void AStarStateSerializer::_read_step_pool(
    ByteStreamReader& r, AStarAlgorithm& astar) const
{
    astar._step_pool.clear();
    uint32_t count = r.u32();
    astar._step_pool.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AStarAlgorithm::StepNode sn;
        sn.prev    = r.i32();
        sn.base_id = r.i32();
        sn.sac_id  = r.i32();
        sn.cost    = r.i32();
        astar._step_pool.push_back(std::move(sn));
    }
}

void AStarStateSerializer::_read_open_heap(
    ByteStreamReader& r, AStarAlgorithm& astar) const
{
    astar._open_heap.clear();
    uint32_t count = r.u32();
    astar._open_heap.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AStarAlgorithm::PriorityEntry entry;
        entry.state.g        = r.i32();
        entry.state.h        = r.i32();
        entry.state.hash     = static_cast<size_t>(r.u64());
        entry.state.step_idx = r.i32();

        uint32_t ids_n = r.u32();
        entry.state.ids.reserve(ids_n);
        for (uint32_t j = 0; j < ids_n; ++j)
            entry.state.ids.push_back(r.i32());

        entry.f = r.i32();
        astar._open_heap.push_back(std::move(entry));
    }
}

void AStarStateSerializer::_read_best_g(
    ByteStreamReader& r, AStarAlgorithm& astar) const
{
    astar._x_import_best_g(r);
}

void AStarStateSerializer::_read_scalars(
    ByteStreamReader& r, AStarAlgorithm& astar) const
{
    astar._best_solution_cost = r.i32();
    astar._solutions_found    = r.i32();
    astar._x_set_explored(r.i64());
}
