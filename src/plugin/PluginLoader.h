#pragma once

/// @file algorithm/PluginLoader.h
/// Scans a directory for algorithm plugin shared libraries (.so / .dll)

#include <mutex>
/// and registers each discovered algorithm with an AlgorithmRegistry.
///
/// Thread-safe after initialization (all mutation happens at load time).

#include "plugin/PluginAPI.h"
#include <string>
#include <vector>

class AlgorithmRegistry;

class PluginLoader {
public:
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    /// Returns the process-wide singleton instance.
    static PluginLoader& instance();

    // ── Loading ────────────────────────────────────────────────────────

    /// Scan a directory for plugin shared libraries and load each one.
    /// Silently skips files that don't expose the required symbols.
    /// Returns the number of plugins successfully loaded.
    /// Thread-safe (acquires a mutex internally).
    size_t load_directory(const std::string& dir_path);

    /// Load a single plugin by its full filesystem path.
    /// Returns nullptr if the file is not a valid plugin.
    /// The returned descriptor is owned by the PluginLoader and remains
    /// valid until unload_all() or destruction.
    const PluginDescriptor* load_plugin(const std::string& so_path);

    // ── Query ──────────────────────────────────────────────────────────

    /// List all currently loaded plugin descriptors.
    std::vector<const PluginDescriptor*> loaded_plugins() const;

    // ── Unload ─────────────────────────────────────────────────────────

    /// Unload a specific plugin by its filesystem path.
    void unload_plugin(const std::string& so_path);

    /// Unload and free all plugins.
    void unload_all();

private:
    PluginLoader() = default;

    struct LoadedPlugin {
        void*   handle{nullptr};
        PluginDescriptor desc;
    };

    PluginDescriptor resolve_symbols(void* handle, const std::string& path);

    std::vector<LoadedPlugin> _plugins;
    mutable class std::mutex* _mtx{nullptr}; // lazily allocated
};
