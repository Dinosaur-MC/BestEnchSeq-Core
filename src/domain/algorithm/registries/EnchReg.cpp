#include "EnchReg.h"
#include <cassert>
#include <stdexcept>

namespace algorithm {

void EnchReg::_build_conflict_matrix() {
    const size_t N = _ench_infos.size();
    _conflict_matrix.assign(N * N, 0);
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            bool conflict =
                _ench_infos[i].is_conflict(_ench_infos[j]) || _ench_infos[j].is_conflict(_ench_infos[i]);
            _conflict_matrix[i * N + j] = conflict ? 1 : 0;
            _conflict_matrix[j * N + i] = conflict ? 1 : 0;
        }
    }
}

void EnchReg::_build_mask_cache() {
    const size_t N = _ench_infos.size();
    _mask_cache.assign(N, 0);
    for (size_t i = 0; i < N; ++i) {
        _mask_cache[i] = _ench_infos[i].exc_mask;
    }
}

void EnchReg::init(
    std::vector<EnchInfo> ench_infos, std::vector<NSID> global_ids, const Equipment &target_equip
) {
    assert(ench_infos.size() == global_ids.size());
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
}

} // namespace algorithm
