#include "CompactedRegistries.h"

namespace compact {

EnchReg &EnchReg::get_instance() {
    static EnchReg instance;
    return instance;
}

void EnchReg::_build_conflict_matrix() {
    const size_t N = _registry.size();
    _conflict_matrix.assign(N, std::vector<char>(N, 0));
    for (size_t i = 0; i < N; ++i) {
        const auto &mask_i = _ench_infos[i].exc_mask;
        for (size_t j = i + 1; j < N; ++j) {
            const auto &mask_j = _ench_infos[j].exc_mask;
            bool conflict = false;
            for (size_t k = 0; k < mask_i.size(); ++k) {
                if (mask_i[k] & mask_j[k]) {
                    conflict = true;
                    break;
                }
            }
            _conflict_matrix[i][j] = conflict;
            _conflict_matrix[j][i] = conflict;
        }
    }
}

void EnchReg::init(const EnchantmentRegistry &registry, const Equipment &target_equip) {
    _registry = registry;
    _target_equip = target_equip;
    _mask_size = _registry.size() / MASK_ELEM_SIZE + 1;
    _ench_infos.resize(_registry.size());
    for (size_t i = 0; i < _registry.size(); ++i) {
        auto &info = _registry.get(i);
        _ench_infos[i].mul = static_cast<int16_t>(info.multiplier);
        _ench_infos[i].max_lvl = static_cast<int16_t>(info.max_level);
        _ench_infos[i].applicable = false;

        for (auto &cat_id : info.applicable_category_ids) {
            if (cat_id == target_equip.category_id) {
                _ench_infos[i].applicable = true;
                break;
            }
        }
        _ench_infos[i].exc_mask.assign(_mask_size, 0);
        _ench_infos[i].exc_mask[i / MASK_ELEM_SIZE] |= 1ULL << (i % MASK_ELEM_SIZE);
        for (auto e : _registry.get_exclusive_set(i)) {
            size_t p = e / MASK_ELEM_SIZE;
            _ench_infos[i].exc_mask[p] |= 1ULL << (e % MASK_ELEM_SIZE);
        }
    }

    _build_conflict_matrix();
}

} // namespace compact
