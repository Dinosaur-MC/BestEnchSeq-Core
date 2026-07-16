#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include "algorithm/strategies/astar/AStarAlgorithm.h"
#include "algorithm/serialization/CompactSerializer.h"
#include "io/ByteStream.h"
#include <chrono>
#include <vector>

// ─── Public interface ────────────────────────────────────────────────────────

std::vector<uint8_t> AStarStateSerializer::serialize(const IAlgorithm& algo) const {
    const auto& astar = static_cast<const AStarAlgorithm&>(algo);
    ByteStreamWriter w;

    // File-level header
    compact_serial::FileHeader fhdr;
    fhdr.magic        = compact_serial::FILE_MAGIC;
    fhdr.version      = compact_serial::FILE_VERSION;
    fhdr.flags        = 0;
    fhdr.num_sections = 5;
    fhdr.timestamp    = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    fhdr.algorithm_tag = std::string(tag());
    compact_serial::write_file_header(w, fhdr);

    // Sections (payload only; total_bytes not used — file header takes over)
    _serialize_item_pool(w, astar);
    _serialize_step_pool(w, astar);
    _serialize_open_heap(w, astar);
    _serialize_best_g(w, astar);
    _serialize_scalars(w, astar);

    return std::move(w).take();
}

void AStarStateSerializer::deserialize(IAlgorithm& algo, std::span<const uint8_t> data) const {
    auto& astar = static_cast<AStarAlgorithm&>(algo);
    ByteStreamReader r(data.data(), data.size());

    // File-level header
    auto hdr = compact_serial::read_file_header(r);
    if (hdr.magic != compact_serial::FILE_MAGIC)
        return;
    // tag is informational; we trust the caller routed correctly

    _deserialize_item_pool(r, astar);
    _deserialize_step_pool(r, astar);
    _deserialize_open_heap(r, astar);
    _deserialize_best_g(r, astar);
    _deserialize_scalars(r, astar);
}

// ─── Serialization helpers ───────────────────────────────────────────────────

void AStarStateSerializer::_serialize_item_pool(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    uint32_t count = static_cast<uint32_t>(astar._pool.size());

    // Build payload: u32(count) + Item[count]
    ByteStreamWriter payload;
    payload.u32(count);
    for (AStarAlgorithm::ItemID i = 0; static_cast<uint32_t>(i) < count; ++i) {
        compact_serial::write(payload, astar._pool[i]);
    }

    auto payload_data = std::move(payload).take();
    compact_serial::write_section_header(w, compact_serial::SECTION_ITEM_POOL,
                                          static_cast<uint32_t>(payload_data.size()));
    w.bytes(payload_data.data(), payload_data.size());
}

void AStarStateSerializer::_serialize_step_pool(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    uint32_t count = static_cast<uint32_t>(astar._step_pool.size());

    // Build payload: u32(count) + StepNode[count]
    // Each StepNode: i32(prev) + i32(base_id) + i32(sac_id) + i32(cost)
    ByteStreamWriter payload;
    payload.u32(count);
    for (const auto& sn : astar._step_pool) {
        payload.i32(sn.prev);
        payload.i32(sn.base_id);
        payload.i32(sn.sac_id);
        payload.i32(sn.cost);
    }

    auto payload_data = std::move(payload).take();
    compact_serial::write_section_header(w, compact_serial::SECTION_STEP_POOL,
                                          static_cast<uint32_t>(payload_data.size()));
    w.bytes(payload_data.data(), payload_data.size());
}

void AStarStateSerializer::_serialize_open_heap(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    uint32_t count = static_cast<uint32_t>(astar._open_heap.size());

    // Build payload: u32(count) + PriorityEntry[count]
    // Each PriorityEntry:
    //   i32(g) + i32(h) + u64(hash) + i32(step_idx)
    //   + u32(ids_count) + i32[ids_count]
    //   + i32(f)
    ByteStreamWriter payload;
    payload.u32(count);
    for (const auto& entry : astar._open_heap) {
        const auto& state = entry.state;
        payload.i32(state.g);
        payload.i32(state.h);
        payload.u64(static_cast<uint64_t>(state.hash));
        payload.i32(state.step_idx);

        uint32_t ids_count = static_cast<uint32_t>(state.ids.size());
        payload.u32(ids_count);
        for (auto id : state.ids)
            payload.i32(id);

        payload.i32(entry.f);
    }

    auto payload_data = std::move(payload).take();
    compact_serial::write_section_header(w, compact_serial::SECTION_OPEN_HEAP,
                                          static_cast<uint32_t>(payload_data.size()));
    w.bytes(payload_data.data(), payload_data.size());
}

void AStarStateSerializer::_serialize_best_g(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    // Build payload via accessor
    ByteStreamWriter payload;
    astar._x_export_best_g(payload);

    auto payload_data = std::move(payload).take();
    compact_serial::write_section_header(w, compact_serial::SECTION_BEST_G,
                                          static_cast<uint32_t>(payload_data.size()));
    w.bytes(payload_data.data(), payload_data.size());
}

void AStarStateSerializer::_serialize_scalars(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    // Build payload: i32(best_solution_cost) + i32(solutions_found) + i64(explored)
    ByteStreamWriter payload;
    payload.i32(astar._best_solution_cost);
    payload.i32(astar._solutions_found);
    payload.i64(astar._x_explored());

    auto payload_data = std::move(payload).take();
    compact_serial::write_section_header(w, compact_serial::SECTION_SCALARS,
                                          static_cast<uint32_t>(payload_data.size()));
    w.bytes(payload_data.data(), payload_data.size());
}

// ─── Deserialization helpers ─────────────────────────────────────────────────

void AStarStateSerializer::_deserialize_item_pool(ByteStreamReader& r, AStarAlgorithm& astar) const {
    if (!compact_serial::check_section_header(r, compact_serial::SECTION_ITEM_POOL))
        return;

    astar._pool.clear();
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i) {
        auto item = compact_serial::read_item(r);
        astar._pool.add(std::move(item));
    }
}

void AStarStateSerializer::_deserialize_step_pool(ByteStreamReader& r, AStarAlgorithm& astar) const {
    if (!compact_serial::check_section_header(r, compact_serial::SECTION_STEP_POOL))
        return;

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

void AStarStateSerializer::_deserialize_open_heap(ByteStreamReader& r, AStarAlgorithm& astar) const {
    if (!compact_serial::check_section_header(r, compact_serial::SECTION_OPEN_HEAP))
        return;

    astar._open_heap.clear();
    uint32_t count = r.u32();
    astar._open_heap.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AStarAlgorithm::PriorityEntry entry;

        entry.state.g        = r.i32();
        entry.state.h        = r.i32();
        entry.state.hash     = static_cast<size_t>(r.u64());
        entry.state.step_idx = r.i32();

        uint32_t ids_count = r.u32();
        entry.state.ids.reserve(ids_count);
        for (uint32_t j = 0; j < ids_count; ++j)
            entry.state.ids.push_back(r.i32());

        entry.f = r.i32();

        astar._open_heap.push_back(std::move(entry));
    }
}

void AStarStateSerializer::_deserialize_best_g(ByteStreamReader& r, AStarAlgorithm& astar) const {
    if (!compact_serial::check_section_header(r, compact_serial::SECTION_BEST_G))
        return;

    astar._x_import_best_g(r);
}

void AStarStateSerializer::_deserialize_scalars(ByteStreamReader& r, AStarAlgorithm& astar) const {
    if (!compact_serial::check_section_header(r, compact_serial::SECTION_SCALARS))
        return;

    astar._best_solution_cost = r.i32();
    astar._solutions_found    = r.i32();
    astar._x_set_explored(r.i64());
}
