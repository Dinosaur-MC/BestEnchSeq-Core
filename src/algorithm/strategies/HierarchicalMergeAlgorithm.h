#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "algorithm/components/AlgorithmDiagnostics.h"
#include "registries/CompactedRegistries.h"
#include <chrono>
#include <cstdint>
#include <vector>

class HierarchicalMergeAlgorithm : public IAlgorithm {
public:
    /// When book count exceeds this threshold, enable pairwise dedup pass.
    /// Tuned empirically — too low triggers overhead for small inputs.
    static constexpr size_t kDedupThreshold = 7;

    explicit HierarchicalMergeAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "hierarchical"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    compact::Item merge_group(
        std::vector<compact::Item>& group,
        std::vector<compact::EnchStep>& steps,
        const compact::EnchReg& reg,
        ExecutionContext& ctx,
        const std::chrono::steady_clock::time_point& start,
        const SearchConfig& search);

    int32_t effective_multiplier(const compact::Item& item, const compact::EnchReg& reg) const;

    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};
