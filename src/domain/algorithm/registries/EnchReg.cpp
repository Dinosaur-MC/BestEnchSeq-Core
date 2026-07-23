#include "EnchReg.h"
#include "common/log/log.hpp"

#include <stdexcept>

namespace algorithm {

void EnchReg::_build_conflict_matrix() {
    const size_t N = _ench_infos.size();
    _conflict_matrix.assign(N * N, 0);
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            bool conflict = _ench_infos[i].is_conflict(_ench_infos[j]);

            _conflict_matrix[i * N + j] = static_cast<char>(conflict);
            _conflict_matrix[j * N + i] = static_cast<char>(conflict);
        }
    }
}

void EnchReg::init(std::vector<EnchInfo> ench_infos, std::vector<int32_t> global_ids,
                   const Equipment &target_equip) {
    _ench_infos  = std::move(ench_infos);
    _global_ids  = std::move(global_ids);
    _target_equip = target_equip;
    _mask_size    = _ench_infos.size() / MASK_ELEM_SIZE + 1;

    _build_conflict_matrix();
}

int16_t EnchReg::to_local_id(int32_t global_id) const {
    for (size_t i = 0; i < _global_ids.size(); ++i) {
        if (_global_ids[i] == global_id)
            return static_cast<int16_t>(i);
    }
    return -1;
}

} // namespace algorithm
