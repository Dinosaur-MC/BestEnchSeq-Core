#pragma once

/// @file loader/AlgorithmLoader.h
/// Unified algorithm lifecycle manager.
///
/// Owns an AlgorithmRegistry and populates it from two sources:
///   1. Compiled-in ("built-in") strategies  — load_builtin()
///   2. Shared-library plugins               — scan_and_load()
///
/// After loading, callers use list() / create() without caring whether
/// an algorithm came from a static link or a .so/.dll.
///
/// Usage (startup):
///
///   AlgorithmLoader loader;
///   loader.load_builtin();
///   loader.scan_and_load("./algorithms");
///   auto algo = loader.create("astar");
///
/// Thread-safety:
///   Mutation (load, unload) intended for startup / shutdown only.
///   create() and list() are safe for concurrent read access.

#include "domain/algorithm/registries/AlgorithmRegistry.h"
#include "PluginAPI.h"

#include <memory>
#include <string>
#include <vector>

namespace algorithm {

class AlgorithmLoader {
  public:
    AlgorithmLoader() = default;
    ~AlgorithmLoader();

    AlgorithmLoader(const AlgorithmLoader &)            = delete;
    AlgorithmLoader &operator=(const AlgorithmLoader &) = delete;

    // ── Registration ──────────────────────────────────────────────────

    /// Register all compiled-in strategies.
    /// Safe to call multiple times — subsequent calls are no-ops.
    void load_builtin();

    /// Scan a directory for plugin shared libraries (.so/.dll).
    /// Returns the number of plugins successfully loaded.
    size_t scan_and_load(const std::string &dir_path);

    /// Load a single plugin by full path. Returns true on success.
    bool load_plugin(const std::string &so_path);

    // ── Query ─────────────────────────────────────────────────────────

    /// List all registered algorithm names (built-in + plugins).
    std::vector<std::string> list() const;
    bool contains(std::string_view name) const;
    size_t size() const;

    // ── Factory ───────────────────────────────────────────────────────

    /// Create an algorithm instance by name. Returns nullptr if unknown.
    std::unique_ptr<IAlgorithm> create(std::string_view name) const;

    // ── Unload ────────────────────────────────────────────────────────

    /// Unload a specific plugin. All instances from it must be destroyed first.
    void unload(const std::string &name);
    void unload_all();

  private:
    struct LoadedPlugin {
        void *handle{nullptr};
        std::string name;
        std::string path; // canonicalized so_path for dedup
    };

    bool resolve_plugin(void *handle, const std::string &path, BesqCreateFn &out_create);

    AlgorithmRegistry _registry;
    std::vector<LoadedPlugin> _plugins;
    bool _builtin_loaded{false};
};

} // namespace algorithm
