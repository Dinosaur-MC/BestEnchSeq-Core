#pragma once
#include "IAlgorithm.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using AlgorithmFactory = std::function<std::unique_ptr<IAlgorithm>()>;

class AlgorithmRegistry {
public:
    static AlgorithmRegistry& instance();

    void register_algorithm(std::string_view name, AlgorithmFactory factory);
    std::unique_ptr<IAlgorithm> create(std::string_view name) const;
    std::vector<std::string> available_algorithms() const;
    bool has_algorithm(std::string_view name) const;

private:
    AlgorithmRegistry() = default;
    std::unordered_map<std::string, AlgorithmFactory> _registry;
};
