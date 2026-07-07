#include "AlgorithmRegistry.h"

AlgorithmRegistry& AlgorithmRegistry::instance() {
    static AlgorithmRegistry inst;
    return inst;
}

void AlgorithmRegistry::register_algorithm(std::string_view name, AlgorithmFactory factory) {
    _registry[std::string(name)] = std::move(factory);
}

std::unique_ptr<IAlgorithm> AlgorithmRegistry::create(std::string_view name) const {
    auto it = _registry.find(std::string(name));
    if (it == _registry.end())
        return nullptr;
    return it->second();
}

std::vector<std::string> AlgorithmRegistry::available_algorithms() const {
    std::vector<std::string> keys;
    keys.reserve(_registry.size());
    for (const auto& [key, _] : _registry)
        keys.push_back(key);
    return keys;
}

bool AlgorithmRegistry::has_algorithm(std::string_view name) const {
    return _registry.find(std::string(name)) != _registry.end();
}
