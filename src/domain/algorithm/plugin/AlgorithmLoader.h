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
///   auto algo = loader.create("dp_merge");
///
/// Thread-safety:
///   Mutation (load, unload) intended for startup / shutdown only.
///   create() and list() are safe for concurrent read access.

#include "domain/algorithm/registries/AlgorithmRegistry.h"
#include "PluginAPI.h"
#include "PluginAudit.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace algorithm {

class AlgorithmLoader {
  public:
    /// Sandbox is enabled when the BESQ_SANDBOX env var is "1" on Linux.
    AlgorithmLoader() {
#if defined(__linux__)
        if (const char *s = std::getenv("BESQ_SANDBOX"); s && std::strcmp(s, "1") == 0)
            _sandbox_enabled = true;
#endif
    }
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

    // ── Sandbox ───────────────────────────────────────────────────────
    /// When enabled, loaded plugins run in a sandboxed besq-worker
    /// subprocess instead of in-process.  (Linux; the subprocess spawn is a
    /// no-op-stub elsewhere.)
    void set_sandbox_enabled(bool on) noexcept { _sandbox_enabled = on; }
    bool sandbox_enabled() const noexcept { return _sandbox_enabled; }

    // ── Security audit ────────────────────────────────────────────────
    /// Standalone scanning: use audit_plugin_binary() from PluginAudit.h.

    // ── Query ─────────────────────────────────────────────────────────

    /// List all registered algorithm names (built-in + plugins).
    std::vector<std::string> list() const;
    bool contains(std::string_view name) const;
    size_t size() const;

    /// Retrieve the audit report for a loaded plugin.
    /// Returns nullptr if the plugin was not audited or not found.
    const PluginAuditReport *get_audit_report(std::string_view name) const;

    // ── Factory ───────────────────────────────────────────────────────

    /// Create an algorithm instance by name. Returns nullptr if unknown.
    std::unique_ptr<IAlgorithm> create(std::string_view name) const;

    // ── Unload ────────────────────────────────────────────────────────

    /// Unload a specific plugin. All instances from it must be destroyed first.
    void unload(const std::string &name);
    void unload_all();

  public:
    /// Retrieve the audit report for the most recently loaded plugin.
    /// Returns nullptr if no plugin has been loaded yet.
    const PluginAuditReport *last_audit() const { return _last_audit ? &_last_audit.value() : nullptr; }

  private:
    struct LoadedPlugin {
        void *handle{nullptr};
        std::string name;
        std::string path; // canonicalized so_path for dedup
        PluginAuditReport audit;
    };

    std::optional<PluginAuditReport> _last_audit;

    bool resolve_plugin(void *handle, const std::string &path, BesqCreateFn &out_create);

    AlgorithmRegistry _registry;
    std::vector<LoadedPlugin> _plugins;
    bool _builtin_loaded{false};
    bool _sandbox_enabled{false};
};

} // namespace algorithm
