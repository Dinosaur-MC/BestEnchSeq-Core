#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "algorithm/components/AlgorithmDiagnostics.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

class HierarchicalMergeAlgorithm : public IAlgorithm {
public:
    explicit HierarchicalMergeAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "hierarchical"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

private:
    compact::Item merge_group(
        std::vector<compact::Item>& group,
        std::vector<compact::EnchStep>& steps,
        const compact::EnchReg& reg,
        ExecutionContext& ctx);

    int32_t effective_multiplier(const compact::Item& item, const compact::EnchReg& reg) const;

    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};
