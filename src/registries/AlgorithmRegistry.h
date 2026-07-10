#pragma once
#include "../algorithm/IAlgorithm.h"
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using AlgorithmFactory = std::function<std::unique_ptr<IAlgorithm>()>;

/// Transparent hash for heterogeneous lookup (std::string_view queries
/// without allocating std::string).
struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
    size_t operator()(const std::string& s) const noexcept { return std::hash<std::string>{}(s); }
};

struct TransparentStringEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    bool operator()(const std::string& a, std::string_view b) const noexcept { return a == b; }
    bool operator()(std::string_view a, const std::string& b) const noexcept { return a == b; }
    bool operator()(const std::string& a, const std::string& b) const noexcept { return a == b; }
};

class AlgorithmRegistry {
public:
    static AlgorithmRegistry& get_instance();

    void register_algorithm(std::string_view name, AlgorithmFactory factory);
    void unregister_algorithm(std::string_view name);
    void clear();
    size_t size() const;
    std::vector<std::string> list() const;
    bool contains(std::string_view name) const;

    // Deprecated — use contains() / list()
    std::unique_ptr<IAlgorithm> create(std::string_view name) const;
    [[deprecated("Use contains() instead")]]
    bool has_algorithm(std::string_view name) const;
    [[deprecated("Use list() instead")]]
    std::vector<std::string> available_algorithms() const;

private:
    AlgorithmRegistry() = default;
    std::unordered_map<std::string, AlgorithmFactory,
                       TransparentStringHash, TransparentStringEqual> _registry;
    mutable std::shared_mutex _mutex;
};
