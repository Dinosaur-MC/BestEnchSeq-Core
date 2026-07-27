#include "EnchReg.h"
#include <cassert>

namespace algorithm {

void EnchReg::_build_conflict_matrix() {
    const size_t N = _ench_infos.size();
    _conflict_matrix.assign(N * N, 0);
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            char is_conflict            = _ench_infos[i].is_conflict(_ench_infos[j]) ? 1 : 0;
            _conflict_matrix[i * N + j] = is_conflict;
            _conflict_matrix[j * N + i] = is_conflict;
        }
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
}

int16_t EnchReg::to_local_id(NSID global_id) const {
    for (size_t i = 0; i < _global_ids.size(); ++i) {
        if (_global_ids[i] == global_id)
            return static_cast<int16_t>(i);
    }
    return -1;
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
        std::vector<int16_t> enchs_vec;
        int32_t dur = 0;
        r >> id_str >> dur >> enchs_vec;
        _target_equip.id             = id_str.empty() ? NSID() : NSID(id_str);
        _target_equip.max_durability = dur;
        _target_equip.applicable_enchs = std::unordered_set<int16_t>(
            enchs_vec.begin(), enchs_vec.end());
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
