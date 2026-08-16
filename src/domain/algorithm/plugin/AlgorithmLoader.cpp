#include "AlgorithmLoader.h"
#include "AppConfig.h"
#include "common/log/log.hpp"
#include "domain/algorithm/_strategies/Registration.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/diagnostics/DiagnosticsService.h"
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/sandbox/SandboxedExecutor.h"
#include "PluginAudit.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

// ─── Platform dlopen/dlsym abstraction ────────────────────────────────

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

void* dl_open(const std::string& path) {
#if defined(_WIN32)
    return static_cast<void*>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* dl_sym(void* handle, const char* symbol) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol));
#else
    return dlsym(handle, symbol);
#endif
}

void dl_close(void* handle) {
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

std::string dl_error() {
#if defined(_WIN32)
    DWORD err = GetLastError();
    if (err == 0)
        return {};
    LPSTR buf = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0,
                               reinterpret_cast<LPSTR>(&buf), 0, nullptr);
    std::string msg(buf, len);
    LocalFree(buf);
    return msg;
#else
    const char* err = dlerror();
    return err ? std::string(err) : std::string{};
#endif
}

} // anonymous namespace

namespace algorithm {

// ====================================================================
// Lifecycle
// ====================================================================

AlgorithmLoader::AlgorithmLoader() {
    // Sandbox mode is global app config (BESQ_SANDBOX), read from the shared
    // AppConfig singleton — callers may still override via set_sandbox_enabled.
    _sandbox_enabled = AppConfig::get().sandbox_enabled;
}

AlgorithmLoader::~AlgorithmLoader() {
    // Process all pending diagnostics events while plugins are still loaded.
    // Plugin-derived objects (e.g. IDAStarDiagnostics) may be referenced by
    // queued events — flushing here ensures their virtual destructors and
    // flush() calls complete before dl_close() unmaps the plugin code.
    DiagnosticsService::instance().flush();
    unload_all();
}

void AlgorithmLoader::load_builtin() {
    if (_builtin_loaded)
        return;
    _builtin_loaded = true;

    besq_register_builtin_strategies(_registry);

    LOG_INFO_ASYNC("Registered %zu built-in algorithm strategy/ies", _registry.size());
}

// ====================================================================
// Plugin loading
// ====================================================================

constexpr const char* SO_EXT =
#if defined(_WIN32)
    ".dll";
#elif defined(__APPLE__)
    ".dylib";
#else
    ".so";
#endif

size_t AlgorithmLoader::scan_and_load(const std::string& dir_path) {
    namespace fs = std::filesystem;

    if (!fs::is_directory(dir_path)) {
        LOG_WARN_ASYNC("Algorithms directory not found: %s", dir_path.c_str());
        return 0;
    }

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (!entry.is_regular_file())
            continue;
        const auto& path = entry.path().string();
        if (!path.ends_with(SO_EXT))
            continue;
        if (load_plugin(path))
            ++count;
    }

    LOG_INFO_ASYNC("Loaded %zu algorithm plugin(s) from %s", count, dir_path.c_str());
    return count;
}

bool AlgorithmLoader::load_plugin(const std::string& so_path) {
    // Avoid double-load on exact path
    auto resolved = std::filesystem::weakly_canonical(so_path).string();
    for (const auto& p : _plugins)
        if (p.path == resolved) {
            LOG_WARN_ASYNC("Algorithm plugin already loaded: %s", so_path.c_str());
            return true;
        }

    // ── Step 1: Pre-load binary audit ────────────────────────────────
    PluginAuditReport audit = audit_plugin_binary(resolved);
    _last_audit = audit;

    // W^X segment is a hard reject — indicates a JIT / code-injection risk.
    if (audit.has_wx_segment) {
        LOG_ERROR_ASYNC("[Audit] REFUSED '%s' — W+X memory segment (exploit risk)", so_path.c_str());
        return false;
    }

    // General audit failure (corrupted / truncated / non-native format).
    if (!audit.passed) {
        LOG_ERROR_ASYNC("[Audit] REFUSED '%s' — binary audit failed (corrupted or "
                  "unrecognized format)",
                  so_path.c_str());
        return false;
    }

    // Log extra exports (if any) — diagnostic noise, not a user-facing warning.
    if (!audit.extra_exports.empty()) {
        // clang-format off
        LOG_DEBUG_ASYNC("[Audit] '%s' exports %zu unexpected symbol(s):", so_path.c_str(),
                  audit.extra_exports.size());
        for (const auto &s : audit.extra_exports)
            LOG_DEBUG_ASYNC("[Audit]   export → %s", s.c_str());
        // clang-format on
    }

    // Log dangerous imports (if any)
    if (!audit.dangerous_imports.empty()) {
        // clang-format off
        LOG_WARN_ASYNC("[Audit] '%s' imports %zu dangerous symbol(s):", so_path.c_str(),
                 audit.dangerous_imports.size());
        for (const auto &s : audit.dangerous_imports)
            LOG_WARN_ASYNC("[Audit]   import → %s", s.c_str());
        // clang-format on
    }

    // Log linked libraries (always informative)
    if (!audit.linked_libraries.empty()) {
        std::string joined;
        for (const auto& lib : audit.linked_libraries) {
            if (!joined.empty())
                joined += ", ";
            joined += lib;
        }
        LOG_INFO_ASYNC("[Audit] '%s' links: %s", so_path.c_str(), joined.c_str());
    }

    // ── Non-sandbox mode: NO containment layer ────────────────────────
    // A plugin that statically imports privileged operations (network /
    // filesystem / process / dynamic-code) would run them IN-PROCESS with
    // full privileges — the audit's warnings alone don't stop it (the
    // malicious test plugin's open("/etc/passwd") ran fine in-process).
    // Refuse instead of warning.  Sandbox mode permits these — seccomp /
    // Job Object physically contain the behavior.
    if (!_sandbox_enabled && audit.limited) {
        // An opaque binary (no section headers / no dynsym) cannot be
        // certified as pure-compute — refuse without a sandbox, same policy
        // as dangerous imports.
        LOG_ERROR_ASYNC("[Audit] REFUSED '%s' — binary audit incomplete (opaque).  "
                  "Run with BESQ_SANDBOX=1 to load it sandboxed.",
                  so_path.c_str());
        return false;
    }
    if (!_sandbox_enabled && !audit.dangerous_imports.empty()) {
        LOG_ERROR_ASYNC("[Audit] REFUSED '%s' — imports %zu dangerous symbol(s) "
                  "(no sandbox).  Run with BESQ_SANDBOX=1 to load it sandboxed.",
                  so_path.c_str(), audit.dangerous_imports.size());
        return false;
    }

    // ── Sandbox mode: NEVER dlopen in the parent ─────────────────────
    // The parent's in-process dlopen of a plugin (bare or shared) crashed
    // the dynamic linker in the large exe.  Instead, probe by spawning a
    // real worker — which loads the plugin in its own small process and
    // returns the name — and register a SandboxedExecutor factory.  This both
    // gets the name and validates the plugin loads under the sandbox.  The
    // sandbox seam is the EXECUTOR, so these register into _sandboxed
    // (IExecutor-typed), NOT _registry (IAlgorithm-typed).
    if (_sandbox_enabled) {
        std::string algo_name;
        try {
            auto probe = std::make_unique<SandboxedExecutor>(resolved, "");
            algo_name = std::string(probe->name());
        } catch (const std::exception& e) {
            LOG_WARN_ASYNC("Plugin '%s' sandbox probe failed: %s", so_path.c_str(), e.what());
            return false;
        }
        // Name collision: a sandboxed plugin must not shadow a trusted builtin
        // (or a previously-loaded plugin) — that would silently change what the
        // name means under BESQ_SANDBOX=1.  Refuse with a clear warning.
        if (_registry.contains(algo_name) || _sandboxed.contains(algo_name)) {
            LOG_WARN_ASYNC("Plugin '%s' name '%s' collides with an existing algorithm — refusing", so_path.c_str(),
                     algo_name.c_str());
            return false;
        }
        _sandboxed[algo_name] = [path = resolved]() { return std::make_unique<SandboxedExecutor>(path, ""); };
        LoadedPlugin plugin;
        plugin.name = algo_name;
        plugin.path = std::move(resolved);
        plugin.audit = std::move(audit);
        _plugins.push_back(std::move(plugin));
        LOG_INFO_ASYNC("Loaded algorithm plugin (sandboxed): %s (from %s)", algo_name.c_str(), so_path.c_str());
        return true;
    }

    // ── Step 2: dlopen (in-process mode only) ────────────────────────
    void* handle = dl_open(resolved);
    if (!handle) {
        LOG_WARN_ASYNC("Failed to load algorithm plugin '%s': %s", so_path.c_str(), dl_error().c_str());
        return false;
    }

    // ── Step 3: Verify required entry point ──────────────────────────
    BesqCreateFn create_fn = nullptr;
    if (!resolve_plugin(handle, so_path, create_fn)) {
        dl_close(handle);
        return false;
    }

    // ── Step 5: Probe (validate ABI by creating an instance) ─────────
    std::unique_ptr<IAlgorithm> probe(static_cast<IAlgorithm*>(create_fn()));
    if (!probe) {
        LOG_WARN_ASYNC("Plugin '%s' returned null from create", so_path.c_str());
        dl_close(handle);
        return false;
    }
    std::string algo_name(probe->name());

    // ── Step 6: Register (in-process mode only) ─────────────────────
    _registry.unregister_algorithm(algo_name);
    _registry.register_algorithm(algo_name, [create_fn]() -> std::unique_ptr<IAlgorithm> {
        void* raw = create_fn();
        if (!raw)
            return nullptr;
        return std::unique_ptr<IAlgorithm>(static_cast<IAlgorithm*>(raw));
    });

    LoadedPlugin plugin;
    plugin.handle = handle;
    plugin.name = algo_name;
    plugin.path = std::move(resolved);
    plugin.audit = std::move(audit);
    _plugins.push_back(std::move(plugin));

    LOG_INFO_ASYNC("Loaded algorithm plugin: %s (from %s)", algo_name.c_str(), so_path.c_str());
    return true;
}

// ====================================================================
// Symbol resolution — looks for one symbol: besq_create_algorithm
// ====================================================================

bool AlgorithmLoader::resolve_plugin(void* handle, const std::string& path, BesqCreateFn& out_create) {
    auto create_fn = reinterpret_cast<BesqCreateFn>(dl_sym(handle, BESQ_PLUGIN_CREATE_SYM));
    if (!create_fn) {
        LOG_WARN_ASYNC("Plugin '%s' missing '%s': %s", path.c_str(), BESQ_PLUGIN_CREATE_SYM, dl_error().c_str());
        return false;
    }

    out_create = create_fn;
    return true;
}

// ====================================================================
// Query — delegate to internal registry
// ====================================================================

std::vector<std::string> AlgorithmLoader::list() const {
    std::vector<std::string> names = _registry.list();
    for (const auto& [name, _] : _sandboxed)
        names.push_back(name);
    return names;
}

bool AlgorithmLoader::contains(std::string_view name) const {
    return _registry.contains(name) || _sandboxed.contains(std::string(name));
}

size_t AlgorithmLoader::size() const {
    return _registry.size() + _sandboxed.size();
}

std::unique_ptr<IAlgorithm> AlgorithmLoader::create(std::string_view name) const {
    // In-process surface: built-ins + in-process plugins only.  Sandboxed
    // plugins live in _sandboxed and are reached via create_executor().
    return _registry.create(name);
}

std::unique_ptr<IExecutor> AlgorithmLoader::create_executor(std::string_view name) const {
    if (_sandbox_enabled) {
        auto it = _sandboxed.find(std::string(name));
        if (it != _sandboxed.end())
            return it->second(); // SandboxedExecutor — worker-hosted real executor
    }
    auto algo = _registry.create(name);
    if (!algo)
        return nullptr;
    // In-process executor.  Built-in strategies are never sandboxed — they are
    // compiled into the trusted kernel, so the registry path is correct for them.
    return std::make_unique<AlgorithmExecutor>(std::move(algo));
}

// ====================================================================
// Audit
// ====================================================================

const PluginAuditReport* AlgorithmLoader::get_audit_report(std::string_view name) const {
    auto it = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto& p) { return p.name == name; });
    return it != _plugins.end() ? &it->audit : nullptr;
}

std::optional<std::string> AlgorithmLoader::plugin_path(std::string_view name) const {
    auto it = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto& p) { return p.name == name; });
    return it != _plugins.end() ? std::optional<std::string>(it->path) : std::nullopt;
}

// ====================================================================
// Unload
// ====================================================================

void AlgorithmLoader::unload(const std::string& name) {
    auto it = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto& p) { return p.name == name; });
    if (it == _plugins.end())
        return;

    _registry.unregister_algorithm(name);
    _sandboxed.erase(name);
    if (it->handle)
        dl_close(it->handle);
    _plugins.erase(it);

    LOG_INFO_ASYNC("Unloaded algorithm plugin: %s", name.c_str());
}

void AlgorithmLoader::unload_all() {
    for (auto& p : _plugins) {
        _registry.unregister_algorithm(p.name);
        if (p.handle)
            dl_close(p.handle);
    }
    _sandboxed.clear();
    _plugins.clear();
}
} // namespace algorithm
