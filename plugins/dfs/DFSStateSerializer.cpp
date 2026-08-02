#include "DFSStateSerializer.h"
#include "DFSAlgorithm.h"
#include "common/io/ByteStream.h"
#include <deque>

namespace algorithm {

// ── DFS-specific section tag constants ──────────────────────────────
namespace {
    constexpr uint32_t TAG_STACK          = 1;
    constexpr uint32_t TAG_FRAME_PAIRS    = 2;
    constexpr uint32_t TAG_VISITED_BEST   = 3;
    constexpr uint32_t TAG_BEST_STEPS     = 4;
    constexpr uint32_t TAG_CURRENT_STEPS  = 5;
    constexpr uint32_t TAG_SCALARS        = 6;

    // Hard upper bounds for deserialized counts (OOM/DoS protection)
    constexpr uint32_t MAX_SERIAL_STACK         = 1'000'000;
    constexpr uint32_t MAX_SERIAL_FRAMEPAIRS    = 1'000'000;
    constexpr uint32_t MAX_SERIAL_VISITED_BEST  = 10'000'000;
    constexpr uint32_t MAX_SERIAL_STEPS         = 10'000'000;
    constexpr uint32_t MAX_SERIAL_PAIRS_PER_FRAME = 10'000;
    constexpr uint32_t MAX_SERIAL_ITEMS_PER_FRAME = 256;
}

// ─── _serialize_state: bundle each internal structure as an opaque section ──

std::vector<checkpoint::Section> DFSStateSerializer::_serialize_state(const IAlgorithm& algo) const {
    const auto& dfs = static_cast<const DFSAlgorithm&>(algo);
    std::vector<checkpoint::Section> sections;
    sections.reserve(6);
    sections.push_back(_write_stack(dfs));
    sections.push_back(_write_frame_pairs(dfs));
    sections.push_back(_write_visited_best(dfs));
    sections.push_back(_write_best_steps(dfs));
    sections.push_back(_write_current_steps(dfs));
    sections.push_back(_write_scalars(dfs));
    return sections;
}

// ─── _deserialize_state: dispatch by section type (masked to logical tag) ──

bool DFSStateSerializer::_deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const {
    auto& dfs = static_cast<DFSAlgorithm&>(algo);

    for (const auto& sect : sections) {
        auto tag = checkpoint::get_algo_tag(sect.header.type);
        ByteStreamReader r(sect.payload.data(), sect.payload.size());
        switch (tag) {
            case TAG_STACK:          _read_stack(r, dfs);          break;
            case TAG_FRAME_PAIRS:    _read_frame_pairs(r, dfs);    break;
            case TAG_VISITED_BEST:   _read_visited_best(r, dfs);   break;
            case TAG_BEST_STEPS:     _read_best_steps(r, dfs);     break;
            case TAG_CURRENT_STEPS:  _read_current_steps(r, dfs);  break;
            case TAG_SCALARS:        _read_scalars(r, dfs);        break;
            default:
                break;  // unknown tag — skip
        }
        if (!r.ok()) return false;
    }

    return true;
}

// ─── Write helpers ───────────────────────────────────────────────────────

checkpoint::Section DFSStateSerializer::_write_stack(const DFSAlgorithm& dfs) {
    ByteStreamWriter payload;
    uint32_t count = static_cast<uint32_t>(dfs._stack.size());
    payload.u32(count);
    for (const auto& frame : dfs._stack) {
        // items (ItemCollection)
        payload.u32(static_cast<uint32_t>(frame.items.size()));
        for (const auto& item : frame.items)
            payload << item;
        // scalar fields
        payload.i32(frame.cost_so_far);
        payload.u64(static_cast<uint64_t>(frame.pair_index));
        payload.u64(static_cast<uint64_t>(frame.saved_steps_size));
        // saved_base and saved_sac (only valid if has_backtrack is true)
        payload << frame.saved_base;
        payload << frame.saved_sac;
        payload.u64(static_cast<uint64_t>(frame.base_idx));
        payload.u64(static_cast<uint64_t>(frame.sac_idx));
        payload.u8(static_cast<uint8_t>(frame.has_backtrack ? 1 : 0));
    }
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_STACK);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

checkpoint::Section DFSStateSerializer::_write_frame_pairs(const DFSAlgorithm& dfs) {
    ByteStreamWriter payload;
    uint32_t count = static_cast<uint32_t>(dfs._frame_pairs.size());
    payload.u32(count);
    for (const auto& pairs_vec : dfs._frame_pairs) {
        payload.u32(static_cast<uint32_t>(pairs_vec.size()));
        for (const auto& pair : pairs_vec) {
            payload.u64(static_cast<uint64_t>(pair.i));
            payload.u64(static_cast<uint64_t>(pair.j));
            payload.i32(pair.est_cost);
        }
    }
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_FRAME_PAIRS);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

checkpoint::Section DFSStateSerializer::_write_visited_best(const DFSAlgorithm& dfs) {
    ByteStreamWriter payload;
    payload.u32(static_cast<uint32_t>(dfs._visited_best.size()));
    for (size_t i = 0; i < dfs._visited_best.bucket_count(); ++i) {
        if (dfs._visited_best.occupied_at(i)) {
            payload.u64(static_cast<uint64_t>(dfs._visited_best.key_at(i)));
            payload.i32(dfs._visited_best.val_at(i));
        }
    }
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_VISITED_BEST);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

checkpoint::Section DFSStateSerializer::_write_best_steps(const DFSAlgorithm& dfs) {
    ByteStreamWriter payload;
    uint32_t count = static_cast<uint32_t>(dfs._best_steps.size());
    payload.u32(count);
    for (const auto& step : dfs._best_steps) {
        payload << step.base << step.sacrifice << step.result;
        payload.i32(step.cost);
    }
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_BEST_STEPS);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

checkpoint::Section DFSStateSerializer::_write_current_steps(const DFSAlgorithm& dfs) {
    ByteStreamWriter payload;
    uint32_t count = static_cast<uint32_t>(dfs._current_steps.size());
    payload.u32(count);
    for (const auto& step : dfs._current_steps) {
        payload << step.base << step.sacrifice << step.result;
        payload.i32(step.cost);
    }
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_CURRENT_STEPS);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

checkpoint::Section DFSStateSerializer::_write_scalars(const DFSAlgorithm& dfs) {
    ByteStreamWriter payload;
    payload.i32(dfs._best_cost);
    payload.i32(dfs._solutions_found);
    checkpoint::Section sect;
    sect.header.type = checkpoint::make_algo_tag(TAG_SCALARS);
    sect.payload = std::move(payload).take();
    sect.header.payload_len = sect.payload.size();
    return sect;
}

// ─── Read helpers ─────────────────────────────────────────────────────────

void DFSStateSerializer::_read_stack(ByteStreamReader& r, DFSAlgorithm& dfs) const {
    dfs._stack.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_STACK) { r.set_fail(); return; }
    dfs._stack.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        DFSAlgorithm::DFSFrame frame;

        // items (ItemCollection)
        uint32_t items_n = r.u32();
        if (items_n > MAX_SERIAL_ITEMS_PER_FRAME) { r.set_fail(); return; }
        frame.items.resize(items_n);
        for (auto& item : frame.items) {
            r >> item;
            if (!r.ok()) break;
        }
        if (!r.ok()) break;

        // scalar fields
        frame.cost_so_far      = r.i32();
        frame.pair_index       = static_cast<size_t>(r.u64());
        frame.saved_steps_size = static_cast<size_t>(r.u64());

        // saved_base and saved_sac
        r >> frame.saved_base;
        r >> frame.saved_sac;
        if (!r.ok()) break;

        frame.base_idx      = static_cast<size_t>(r.u64());
        frame.sac_idx       = static_cast<size_t>(r.u64());
        frame.has_backtrack = r.u8() != 0;

        if (!r.ok()) break;
        dfs._stack.push_back(std::move(frame));
    }
}

void DFSStateSerializer::_read_frame_pairs(ByteStreamReader& r, DFSAlgorithm& dfs) const {
    dfs._frame_pairs.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_FRAMEPAIRS) { r.set_fail(); return; }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pairs_n = r.u32();
        if (pairs_n > MAX_SERIAL_PAIRS_PER_FRAME) { r.set_fail(); return; }
        std::vector<DFSAlgorithm::ForgePair> pairs_vec;
        pairs_vec.reserve(pairs_n);
        for (uint32_t j = 0; j < pairs_n; ++j) {
            if (!r.ok()) break;
            DFSAlgorithm::ForgePair fp;
            fp.i        = static_cast<size_t>(r.u64());
            fp.j        = static_cast<size_t>(r.u64());
            fp.est_cost = r.i32();
            pairs_vec.push_back(std::move(fp));
        }
        if (!r.ok()) break;
        dfs._frame_pairs.push_back(std::move(pairs_vec));
    }
}

void DFSStateSerializer::_read_visited_best(ByteStreamReader& r, DFSAlgorithm& dfs) const {
    dfs._visited_best.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_VISITED_BEST) { r.set_fail(); return; }
    for (uint32_t i = 0; i < count; ++i) {
        size_t key = static_cast<size_t>(r.u64());
        int32_t val = r.i32();
        if (!r.ok()) break;
        dfs._visited_best[key] = val;
    }
}

void DFSStateSerializer::_read_best_steps(ByteStreamReader& r, DFSAlgorithm& dfs) const {
    dfs._best_steps.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_STEPS) { r.set_fail(); return; }
    dfs._best_steps.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        EnchStep step;
        r >> step.base >> step.sacrifice >> step.result;
        step.cost = r.i32();
        if (!r.ok()) break;
        dfs._best_steps.push_back(std::move(step));
    }
}

void DFSStateSerializer::_read_current_steps(ByteStreamReader& r, DFSAlgorithm& dfs) const {
    dfs._current_steps.clear();
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_STEPS) { r.set_fail(); return; }
    dfs._current_steps.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        EnchStep step;
        r >> step.base >> step.sacrifice >> step.result;
        step.cost = r.i32();
        if (!r.ok()) break;
        dfs._current_steps.push_back(std::move(step));
    }
}

void DFSStateSerializer::_read_scalars(ByteStreamReader& r, DFSAlgorithm& dfs) const {
    dfs._best_cost      = r.i32();
    dfs._solutions_found = r.i32();
}

} // namespace algorithm
