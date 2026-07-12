#include "CompactedRegistries.h"

#include <iostream>
#include <stdexcept>

namespace compact {

void EnchReg::_build_conflict_matrix() {
    const size_t N = _registry.size();
    _conflict_matrix.assign(N * N, 0);
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            bool conflict = _registry.is_incompatible(static_cast<int32_t>(i),
                                                       static_cast<int32_t>(j));
            _conflict_matrix[i * N + j] = static_cast<char>(conflict);
            _conflict_matrix[j * N + i] = static_cast<char>(conflict);
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
        // Saturating conversion: if values exceed int16_t range, clamp and warn
        if (info.multiplier > INT16_MAX) {
            _ench_infos[i].mul = INT16_MAX;
            std::cerr << "[ench_reg] multiplier " << info.multiplier
                      << " at index " << i << " exceeds INT16_MAX, clamped\n";
        } else if (info.multiplier < 0) {
            _ench_infos[i].mul = 0;
        } else {
            _ench_infos[i].mul = static_cast<int16_t>(info.multiplier);
        }
        if (info.max_level > INT16_MAX) {
            _ench_infos[i].max_lvl = INT16_MAX;
            std::cerr << "[ench_reg] max_level " << info.max_level
                      << " at index " << i << " exceeds INT16_MAX, clamped\n";
        } else if (info.max_level < 0) {
            _ench_infos[i].max_lvl = 0;
        } else {
            _ench_infos[i].max_lvl = static_cast<int16_t>(info.max_level);
        }
        _ench_infos[i].applicable = false;

        for (auto &cat_id : info.applicable_category_ids) {
            if (cat_id == target_equip.category_id) {
                _ench_infos[i].applicable = true;
                break;
            }
        }
        _ench_infos[i].exc_mask.assign(_mask_size, 0);
        for (auto e : _registry.get_exclusive_set(i)) {
            if (e < 0)
                throw std::out_of_range("Negative exclusive-set enchantment id in EnchReg::init()");
            size_t p = static_cast<size_t>(e) / MASK_ELEM_SIZE;
            _ench_infos[i].exc_mask[p] |= 1ULL << (static_cast<size_t>(e) % MASK_ELEM_SIZE);
        }
    }

    _build_conflict_matrix();
}

} // namespace compact
