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

void EnchReg::init(const EnchInfo &ench_infos, const Equipment &target_equip) {
    _target_equip = target_equip;
    _mask_size    = _ench_infos.size() / MASK_ELEM_SIZE + 1;

    _build_conflict_matrix();
}

} // namespace algorithm
