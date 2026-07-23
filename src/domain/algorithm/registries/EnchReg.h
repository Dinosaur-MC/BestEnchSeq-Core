#pragma once
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
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
    std::vector<EnchInfo> _ench_infos;   // compacted info, indexed by local ench id
    std::vector<int32_t> _global_ids;     // local → business global ID (for round-trip)
    Equipment _target_equip;
    size_t _mask_size; // exc_mask vector size

    std::vector<char> _conflict_matrix; // flat N×N, row-major
    void _build_conflict_matrix();

  public:
    EnchReg() = default;

    /// Initialize with compact enchantment info and target equipment.
    /// `global_ids` maps each local index to its original business-registry ID,
    /// enabling the reverse mapping in AlgorithmOutput → domain conversion.
    void init(std::vector<EnchInfo> ench_infos, std::vector<int32_t> global_ids,
              const Equipment &target_equip);

    [[nodiscard]] size_t size() const noexcept { return _ench_infos.size(); }
    [[nodiscard]] size_t get_mask_size() const noexcept { return _mask_size; }

    [[nodiscard]] const Equipment &get_target_equip() const noexcept { return _target_equip; }
    [[nodiscard]] const EnchInfo &get(int16_t id) const { return _ench_infos.at(id); }
    /// Bounds-unchecked access — hot-path design.
    [[nodiscard]] const EnchInfo &operator[](int16_t id) const noexcept { return _ench_infos[id]; }
    [[nodiscard]] bool is_conflict(int16_t id1, int16_t id2) const noexcept {
        return _conflict_matrix[id1 * _ench_infos.size() + id2];
    }

    /// Convert between local (compact) and global (business) enchantment IDs.
    [[nodiscard]] int32_t to_global_id(int16_t local_id) const { return _global_ids.at(local_id); }
    [[nodiscard]] int16_t to_local_id(int32_t global_id) const; // -1 if not found

};

} // namespace algorithm
