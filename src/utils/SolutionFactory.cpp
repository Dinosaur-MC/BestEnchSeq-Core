#include "SolutionFactory.h"

// Note: Full create() implementation requires AlgorithmOutput (defined in Task 4).
// For now the method body is a stub that returns an empty vector.
std::vector<EnchSolution> SolutionFactory::create(
    platform::MCE platform,
    const EnchSet& original_ench,
    const ItemStack& target_item,
    const ItemCollection& available_items,
    const AlgorithmOutput& output)
{
    (void)platform;
    (void)original_ench;
    (void)target_item;
    (void)available_items;
    (void)output;
    // TODO: iterate output.steps and call create_single for each step list
    return {};
}

EnchSolution SolutionFactory::create_single(
    platform::MCE platform,
    const EnchSet& original_ench,
    const ItemStack& target_item,
    const ItemCollection& available_items,
    const EnchStepList& steps,
    std::string_view algo_name,
    std::string_view algo_version,
    bool is_valid)
{
    return EnchSolution::make(
        platform, original_ench, target_item, available_items,
        steps, is_valid,
        EnchSolution::MetaData{
            std::string(algo_name),
            std::string(algo_version),
            0, 0  // created_at, computation_time — filled by AlgorithmOutput
        }
    );
}
