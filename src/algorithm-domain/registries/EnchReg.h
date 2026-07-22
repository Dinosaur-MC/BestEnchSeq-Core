#pragma once
#include "../types/Enchantment.h"
#include "../types/Equipment.h"
#include <vector>

// ─── Forward declarations for serialization friends ───────────────────────
class ByteStreamWriter;
class ByteStreamReader;

namespace algorithm {
class EnchReg;
}

namespace compact_serial {
void write(ByteStreamWriter &w, const algorithm::EnchReg &reg);
algorithm::EnchReg read_ench_reg(ByteStreamReader &r);
} // namespace compact_serial

namespace algorithm {

/// Compacted registry — precomputes EnchInfo for all enchantments against
/// a specific target equipment. Provides O(1) lookup and conflict checking.
class EnchReg {
    friend void compact_serial::write(ByteStreamWriter &w, const EnchReg &reg);
    friend EnchReg compact_serial::read_ench_reg(ByteStreamReader &r);

  private:
    std::vector<EnchInfo> _ench_infos; // compacted info, indexed by ench id
    Equipment _target_equip;
    size_t _mask_size; // exc_mask vector size

    std::vector<char> _conflict_matrix; // flat N×N, row-major
    void _build_conflict_matrix();

  public:
    EnchReg() = default;

    void init(const EnchInfo &ench_infos, const Equipment &target_equip);

    [[nodiscard]] size_t size() const noexcept { return _ench_infos.size(); }
    [[nodiscard]] size_t get_mask_size() const noexcept { return _mask_size; }

    [[nodiscard]] const Equipment &get_target_equip() const noexcept { return _target_equip; }
    [[nodiscard]] const EnchInfo &get(int16_t id) const { return _ench_infos.at(id); }
    /// Bounds-unchecked access — hot-path design.
    ///
    /// Intentionally uses `vector::operator[]` (no bounds check) unlike `get()`
    /// which uses `.at()`.  All call paths in algorithm inner loops (`is_conflict`)
    /// receive IDs from the pre-validated compact registry subset, so the check
    /// would be redundant overhead.  Callers MUST ensure `0 <= id < size()`.
    /// Use `reg[id].mul`, `.mul_b`, `.max_lvl` to access compact EnchInfo fields.
    [[nodiscard]] const EnchInfo &operator[](int16_t id) const noexcept { return _ench_infos[id]; }
    [[nodiscard]] bool is_conflict(int16_t id1, int16_t id2) const noexcept {
        return _conflict_matrix[id1 * _ench_infos.size() + id2];
    }

};

} // namespace algorithm
