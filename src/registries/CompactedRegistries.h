#pragma once
#include "registries/EnchantmentRegistry.h"
#include "types/CompactedTypes.h"
#include "types/Equipment.h"
#include <vector>

namespace compact {

using RichEnchInfo = ::EnchInfo;

/// Compacted registry — precomputes EnchInfo for all enchantments against
/// a specific target equipment. Provides O(1) lookup and conflict checking.
class EnchReg {
  private:
    EnchantmentRegistry _registry;     // sub-registry (copied for lifetime safety)
    std::vector<EnchInfo> _ench_infos; // compacted info, indexed by ench id
    Equipment _target_equip;
    size_t _mask_size; // exc_mask vector size

    std::vector<char> _conflict_matrix; // flat N×N, row-major
    void _build_conflict_matrix();

  public:
    EnchReg() = default;

    void init(const EnchantmentRegistry &registry, const Equipment &target_equip);

    size_t size() const noexcept { return _ench_infos.size(); }
    size_t get_mask_size() const noexcept { return _mask_size; }

    const Equipment &get_target_equip() const noexcept { return _target_equip; }
    const RichEnchInfo &get_rich(int16_t id) const { return _registry.get(id); }
    const EnchInfo &get(int16_t id) const { return _ench_infos.at(id); }
    /// Bounds-unchecked access — hot-path design.
    ///
    /// Intentionally uses `vector::operator[]` (no bounds check) unlike `get()`
    /// which uses `.at()`.  All call paths in algorithm inner loops
    /// (`get_multiplier`, `get_max_level`, `is_conflict`) receive IDs from
    /// the pre-validated compact registry subset, so the check would be
    /// redundant overhead.  Callers MUST ensure `0 <= id < size()`.
    const EnchInfo &operator[](int16_t id) const noexcept { return _ench_infos[id]; }

    uint16_t get_multiplier(int16_t id) const noexcept { return (*this)[id].mul; }
    uint16_t get_max_level(int16_t id) const noexcept { return (*this)[id].max_lvl; }
    bool is_conflict(int16_t id1, int16_t id2) const noexcept { return _conflict_matrix[id1 * _ench_infos.size() + id2]; }

    int32_t to_local_id(int32_t global_id) const { return _registry.to_local_id(global_id); }
    const EnchantmentRegistry& get_registry() const { return _registry; }
};

} // namespace compact
