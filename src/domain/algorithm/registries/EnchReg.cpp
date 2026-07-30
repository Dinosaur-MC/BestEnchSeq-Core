#include "EnchReg.h"
#include <cassert>
#include <stdexcept>

namespace algorithm {

void EnchReg::_build_conflict_matrix() {
    _conflict_matrix.fill(0);
    const size_t N = _ench_infos.size();
    constexpr size_t Stride = EnchSet::MAX_SIZE;
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            bool conflict =
                _ench_infos[i].is_conflict(_ench_infos[j]) || _ench_infos[j].is_conflict(_ench_infos[i]);
            _conflict_matrix[i * Stride + j] = conflict ? 1 : 0;
            _conflict_matrix[j * Stride + i] = conflict ? 1 : 0;
        }
    }
}

void EnchReg::_build_mask_cache() {
    _mask_cache.fill(0);
    const size_t N = _ench_infos.size();
    constexpr size_t Stride = EnchSet::MAX_SIZE;
    // exc_mask only stores one direction of exclusive_set (the current ench's
    // exclusive_set against already-added enchantments). The conflict matrix
    // is symmetric after _build_conflict_matrix() computes both directions.
    // We reconstruct the full mask from the matrix so that get_conflict_mask()
    // returns ALL conflicts, not just the one-direction bit.
    for (size_t i = 0; i < N; ++i) {
        mask_type mask = 0;
        for (size_t j = 0; j < N; ++j) {
            if (_conflict_matrix[i * Stride + j])
                mask |= (mask_type{1} << j);
        }
        _mask_cache[i] = mask;
    }
}

void EnchReg::init(
    std::vector<EnchInfo> ench_infos, std::vector<NSID> global_ids, const Equipment &target_equip
) {
    assert(ench_infos.size() == global_ids.size());
    assert(ench_infos.size() <= EnchSet::MAX_SIZE);
    _ench_infos   = std::move(ench_infos);
    _global_ids   = std::move(global_ids);
    _target_equip = target_equip;
    _build_conflict_matrix();
    _build_mask_cache();
}

EnchReg::id_type EnchReg::to_local_id(NSID global_id) const {
    for (size_t i = 0; i < _global_ids.size(); ++i) {
        if (_global_ids[i] == global_id)
            return static_cast<id_type>(i);
    }
    throw std::out_of_range("No local id found for global id");
}

// ── Serialization ──
void EnchReg::serialize(ByteStreamWriter &w) const noexcept {
    w << _ench_infos << _target_equip;
    // Serialize _global_ids manually since NSID is not TrivialSerializable
    w << _global_ids.size();
    for (const auto &nsid : _global_ids)
        w << nsid.str();
}
void EnchReg::deserialize(ByteStreamReader &r) noexcept {
    r >> _ench_infos;
    assert(_ench_infos.size() <= EnchSet::MAX_SIZE);
    // Manual Equipment deserialize — NSID("") would throw, so use default ctor
    // when the id string is empty.
    {
        std::string id_str;
        std::vector<uint8_t> enchs_vec;
        int32_t dur = 0;
        r >> id_str >> dur >> enchs_vec;
        _target_equip.id               = id_str.empty() ? NSID() : NSID(id_str);
        _target_equip.max_durability   = dur;
        _target_equip.applicable_enchs = std::unordered_set<uint8_t>(enchs_vec.begin(), enchs_vec.end());
    }
    // Deserialize _global_ids manually — same NSID("") guard.
    size_t n = 0;
    r >> n;
    _global_ids.resize(n);
    for (size_t i = 0; i < n; ++i) {
        std::string s;
        r >> s;
        _global_ids[i] = s.empty() ? NSID() : NSID(s);
    }
    _build_conflict_matrix();
    _build_mask_cache();
}

} // namespace algorithm
