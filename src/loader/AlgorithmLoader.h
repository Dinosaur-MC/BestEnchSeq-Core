#pragma once

/// @file loader/AlgorithmLoader.h
/// Unified algorithm lifecycle manager.
///
/// Owns an AlgorithmRegistry and populates it from two sources:
///   1. Compiled-in ("built-in") strategies  — load_builtin()
///   2. Shared-library plugins               — scan_and_load()
///
/// After loading, callers use list() / create() through this class without
/// caring whether an algorithm came from a static link or a .so/.dll.
///
/// Usage (startup):
///
///   AlgorithmLoader loader;
///   loader.load_builtin();                     // compiled-in strategies
///   loader.scan_and_load("./algorithms");       // plugin .so/.dll files
///   auto algo = loader.create("astar");        // opaque origin
///
/// Thread-safety:
///   All mutation (load, unload) is intended for startup / shutdown only.
///   create() and list() are safe for concurrent read access.

#include "loader/PluginAPI.h"
#include "registries/AlgorithmRegistry.h"

#include <memory>
#include <string>
#include <vector>

class IAlgorithm;

class AlgorithmLoader {
public:
    AlgorithmLoader() = default;
    ~AlgorithmLoader();

    AlgorithmLoader(const AlgorithmLoader&) = delete;
    AlgorithmLoader& operator=(const AlgorithmLoader&) = delete;

    // ── Registration ──────────────────────────────────────────────────

    /// Register all compiled-in strategies (hamming, dfs, astar, …).
    /// Safe to call multiple times — subsequent calls are no-ops.
    void load_builtin();

    /// Scan a directory for plugin shared libraries (.so/.dll) and load
    /// each one, registering its algorithm factory into the internal
    /// registry.  Silently skips files that don't expose the required
    /// symbols.
    /// Returns the number of plugins successfully loaded.
    size_t scan_and_load(const std::string& dir_path);

    /// Load a single plugin by its full filesystem path.
    /// Returns true on success.
    bool load_plugin(const std::string& so_path);

    // ── Query ─────────────────────────────────────────────────────────

    /// List all registered algorithm names (built-in + plugins).
    std::vector<std::string> list() const;

    /// Check whether an algorithm is registered.
    bool contains(std::string_view name) const;

    /// Number of registered algorithms.
    size_t size() const;

    // ── Factory ───────────────────────────────────────────────────────

    /// Create an algorithm instance by name.
    /// Returns nullptr if the name is unknown.
    std::unique_ptr<IAlgorithm> create(std::string_view name) const;

    // ── Unload ────────────────────────────────────────────────────────

    /// Unload a specific plugin by name and remove it from the registry.
    /// All IAlgorithm instances created from this plugin MUST have been
    /// destroyed beforehand (otherwise the dlclose'd vtables will dangle).
    void unload(const std::string& name);

    /// Unload all plugins.  Same lifetime precondition as unload().
    void unload_all();

    // ── Utilities ─────────────────────────────────────────────────────

    /// Resolve the default algorithms directory relative to the current
    /// executable: `<exe_dir>/algorithms/`.
    static std::string default_algorithms_dir();

private:
    struct LoadedPlugin {
        void*                  handle{nullptr};
        std::string            name;
        std::string            path;
        BesqAlgorithmCreateFn  create{nullptr};
        BesqAlgorithmDestroyFn destroy{nullptr};
    };

    // Resolve C ABI symbols from a dlopen'd handle.
    // Returns invalid descriptor (all nullptr) on failure.
    BesqPluginDescriptor resolve_symbols(void* handle, const std::string& path);

    AlgorithmRegistry _registry;
    std::vector<std::unique_ptr<LoadedPlugin>> _plugins;
    bool _builtin_loaded{false};
};
