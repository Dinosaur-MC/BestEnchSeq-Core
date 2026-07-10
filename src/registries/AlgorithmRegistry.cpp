#include "AlgorithmRegistry.h"
#include <mutex>

AlgorithmRegistry& AlgorithmRegistry::get_instance() {
    static AlgorithmRegistry instance;
    return instance;
}

void AlgorithmRegistry::register_algorithm(std::string_view name, AlgorithmFactory factory) {
    std::unique_lock lock(_mutex);
    _registry[std::string(name)] = std::move(factory);
}

void AlgorithmRegistry::unregister_algorithm(std::string_view name) {
    std::unique_lock lock(_mutex);
    _registry.erase(_registry.find(name), _registry.end());
}

void AlgorithmRegistry::clear() {
    std::unique_lock lock(_mutex);
    _registry.clear();
}

size_t AlgorithmRegistry::size() const {
    std::shared_lock lock(_mutex);
    return _registry.size();
}

std::vector<std::string> AlgorithmRegistry::list() const {
    std::shared_lock lock(_mutex);
    std::vector<std::string> keys;
    keys.reserve(_registry.size());
    for (const auto& [key, _] : _registry)
        keys.push_back(key);
    return keys;
}

bool AlgorithmRegistry::contains(std::string_view name) const {
    std::shared_lock lock(_mutex);
    return _registry.find(name) != _registry.end();
}

std::unique_ptr<IAlgorithm> AlgorithmRegistry::create(std::string_view name) const {
    std::shared_lock lock(_mutex);
    auto it = _registry.find(name);
    if (it == _registry.end())
        return nullptr;
    return it->second();
}

