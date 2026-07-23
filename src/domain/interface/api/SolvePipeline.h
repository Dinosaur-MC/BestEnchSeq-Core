#pragma once
#include "domain/business/types/Item.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Solution.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include <cstdint>
#include <string>
#include <vector>

#include "domain/algorithm/types/AlgorithmTypes.h"

// Forward declarations
class EnchantmentRegistry;
class EquipmentRegistry;
class EquipmentCategoryRegistry;
namespace algorithm { class AlgorithmLoader; struct ResolvedInput; }

/// Input to the solve pipeline.
///
/// target_item carries the target equipment plus its DESIRED final
/// enchantments (target_ench).  source_enchantments describes what
/// the equipment already has.  The pipeline computes the diff.
struct SolveInput {
    Item target_item;                  ///< Equipment piece with desired enchants
    EnchSet source_enchantments;            ///< Enchantments already on the equipment
    algorithm::ForgeConfig forge_config;               ///< Forge behaviour flags
    algorithm::SearchConfig search_config;             ///< Search limits (solutions, memory, time)
    std::string algorithm = "hamming";      ///< Algorithm strategy name
    std::vector<Item> extra_items;     ///< Additional items (inventory mode)
    bool is_inventory_mode = false;         ///< Whether extra_items replaces book generation
};

/// Result of a solve attempt.
struct SolveResult {
    bool success = false;                                    ///< At least one solution found
    std::vector<Solution> solutions;                     ///< Domain-level solutions
    std::string algorithm_used;                              ///< Algorithm that produced the result
    int64_t computation_time_ms = 0;                         ///< Wall-clock execution time

    std::string to_json(const EnchantmentRegistry& ench_reg,
                        const EquipmentCategoryRegistry& cat_reg) const;
    std::string to_text(const EnchantmentRegistry& ench_reg,
                        const EquipmentCategoryRegistry& cat_reg) const;

    /// Raw JSON output — no registry dependencies, uses raw IDs and cost info only.
    std::string to_json_raw() const;
};

namespace detail {

/// Internal result of the execute stage — carries AlgorithmOutput for recall().
struct ExecuteResult {
    algorithm::AlgorithmOutput algo_output;
    int64_t computation_time_ms = 0;
    std::string algorithm_name;
};

/// Internal solve pipeline — breaks the algorithm into
/// reusable stages for testability and library usage.
class SolvePipeline {
public:
    /// Run the full pipeline: resolve → apply → execute → recall.
    static SolveResult run(
        const SolveInput& input,
        const algorithm::AlgorithmLoader& loader,
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& eq_reg,
        const EquipmentCategoryRegistry& cat_reg);

    /// Stage 1: Domain resolution.  Validates inputs and builds
    /// algorithm::ResolvedInput (with graduated books for direct mode).
    static algorithm::ResolvedInput resolve(const SolveInput& input,
                                            const EnchantmentRegistry& ench_reg,
                                            const EquipmentRegistry& eq_reg);

    /// Stage 2: Domain → compact conversion (via CompactAdapter).
    static algorithm::AlgorithmInput apply(const algorithm::ResolvedInput& resolved,
                                           const ::Equipment& target_equipment,
                                           const EnchantmentRegistry& ench_reg);

    /// Stage 3: Compact algorithm execution.
    /// Creates the requested strategy, checks mode support, runs the executor.
    static ExecuteResult execute(algorithm::AlgorithmInput& algo_input,
                                 const std::string& algorithm,
                                 const algorithm::AlgorithmLoader& loader);

    /// Stage 4: Compact → domain conversion (via CompactAdapter::recall)
    /// wrapped in a SolveResult.
    static SolveResult recall(const algorithm::AlgorithmOutput& output,
                              const algorithm::AlgorithmInput& algo_input,
                              const algorithm::ResolvedInput& resolved);
};

} // namespace detail
