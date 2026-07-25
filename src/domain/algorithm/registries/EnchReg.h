#pragma once
#include "CommonTypes.h"
#include "common/serialization/IBinarySerializable.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include <vector>

namespace algorithm {
class EnchReg;
}

namespace algorithm {

/// Compacted registry — precomputes EnchInfo for all enchantments against
/// a specific target equipment. Provides O(1) lookup and conflict checking.
class EnchReg : public IBinarySerializable {
  private:
    std::vector<EnchInfo> _ench_infos;   // compacted info, indexed by local ench id
    std::vector<NSID> _global_ids;     // local → business global ID (for round-trip)
    Equipment _target_equip;

    std::vector<char> _conflict_matrix; // flat N×N, row-major
    void _build_conflict_matrix();

  public:
    EnchReg() = default;

    /// Initialize with compact enchantment info and target equipment.
    /// `global_ids` maps each local index to its original business-registry ID,
    /// enabling the reverse mapping in AlgorithmOutput → domain conversion.
    void init(std::vector<EnchInfo> ench_infos, std::vector<NSID> global_ids,
              const Equipment &target_equip);

    [[nodiscard]] size_t size() const noexcept { return _ench_infos.size(); }

    [[nodiscard]] const Equipment &get_target_equip() const noexcept { return _target_equip; }
    [[nodiscard]] const EnchInfo &get(int16_t id) const { return _ench_infos.at(id); }
    /// Bounds-unchecked access — hot-path design.
    [[nodiscard]] const EnchInfo &operator[](int16_t id) const noexcept { return _ench_infos[id]; }
    [[nodiscard]] bool is_conflict(int16_t id1, int16_t id2) const noexcept {
        return _conflict_matrix[id1 * _ench_infos.size() + id2];
    }

    /// Convert between local (compact) and global (business) enchantment IDs.
    [[nodiscard]] NSID to_global_id(int16_t local_id) const { return _global_ids.at(local_id); }
    [[nodiscard]] int16_t to_local_id(NSID global_id) const; // -1 if not found

    // ── Serialization ──
    void serialize(ByteStreamWriter &w) const noexcept override;
    void deserialize(ByteStreamReader &r) noexcept override;
};

} // namespace algorithm
