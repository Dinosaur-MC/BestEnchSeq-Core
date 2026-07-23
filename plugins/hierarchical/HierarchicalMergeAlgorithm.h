#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <chrono>
#include <cstdint>
#include <vector>

namespace algorithm {

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
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    Item merge_group(
        std::vector<Item>& group,
        std::vector<EnchStep>& steps,
        const EnchReg& reg,
        ExecutionContext& ctx,
        const std::chrono::steady_clock::time_point& start,
        const SearchConfig& search);

    int32_t effective_multiplier(const Item& item, const EnchReg& reg) const;

    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};

} // namespace algorithm
