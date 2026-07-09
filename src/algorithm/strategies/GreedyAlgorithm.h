#pragma once
#include "../IAlgorithm.h"
#include "../forge/DefaultForgeEngine.h"

class GreedyAlgorithm : public IAlgorithm {
public:
    explicit GreedyAlgorithm(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "greedy"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _forge_engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    DefaultForgeEngine _forge_engine;
};
