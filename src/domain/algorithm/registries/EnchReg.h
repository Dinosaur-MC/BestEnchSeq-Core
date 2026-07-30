#pragma once
#include "domain/algorithm/types/EnchSet.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include <array>
#include <vector>

namespace algorithm {
class EnchReg;
}

namespace algorithm {

/// Compacted registry — precomputes EnchInfo for all enchantments against
/// a specific target equipment. Provides O(1) lookup and conflict checking.
class EnchReg : public IBinarySerializable {
  public:
    using id_type = Ench::value_type; // uint8_t

  private:
    std::vector<EnchInfo> _ench_infos; // compacted info, indexed by local ench id
    std::vector<NSID> _global_ids;     // local → business global ID (for round-trip)
    Equipment _target_equip;

    std::array<mask_type, EnchSet::MAX_SIZE> _mask_cache; // precomputed conflict masks
    void _build_mask_cache();
    
    std::array<char, EnchSet::MAX_SIZE * EnchSet::MAX_SIZE> _conflict_matrix; // flat N×N, row-major
    void _build_conflict_matrix();

  public:
    EnchReg() = default;

    /// Initialize with compact enchantment info and target equipment.
    /// `global_ids` maps each local index to its original business-registry ID,
    /// enabling the reverse mapping in AlgorithmOutput → domain conversion.
    void init(std::vector<EnchInfo> ench_infos, std::vector<NSID> global_ids, const Equipment &target_equip);

    [[nodiscard]] size_t size() const noexcept { return _ench_infos.size(); }
    [[nodiscard]] bool empty() const noexcept { return _ench_infos.empty(); }
    [[nodiscard]] const std::vector<EnchInfo> &get_ench_infos() const noexcept { return _ench_infos; }
    [[nodiscard]] const std::vector<NSID> &get_global_ids() const noexcept { return _global_ids; }

    [[nodiscard]] const EnchInfo &get(id_type id) const { return _ench_infos.at(id); }
    /// Bounds-unchecked access — hot-path design.
    [[nodiscard]] const EnchInfo &operator[](id_type id) const noexcept { return _ench_infos[id]; }

    [[nodiscard]] bool is_applicable(id_type id) const noexcept {
        return _target_equip.applicable_enchs.contains(id);
    }
    [[nodiscard]] bool is_conflict(id_type id1, id_type id2) const noexcept {
        return _conflict_matrix[static_cast<size_t>(id1) * EnchSet::MAX_SIZE + id2];
    }
    [[nodiscard]] mask_type get_conflict_mask(id_type id) const noexcept { return _mask_cache[id]; }
    [[nodiscard]] const Equipment &get_target_equip() const noexcept { return _target_equip; }

    /// Convert between local (compact) and global (business) enchantment IDs.
    [[nodiscard]] NSID to_global_id(id_type local_id) const { return _global_ids.at(local_id); }
    [[nodiscard]] id_type to_local_id(NSID global_id) const; // throw out_of_range if not found

    // ── Serialization ──
    void serialize(ByteStreamWriter &w) const noexcept override;
    void deserialize(ByteStreamReader &r) noexcept override;
};

} // namespace algorithm
