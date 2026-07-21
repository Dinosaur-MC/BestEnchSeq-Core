#include "algorithm/IAlgorithm.h"
#include "loader/AlgorithmLoader.h"
#include "log/log.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>

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

void *dl_open(const std::string &path) {
#if defined(_WIN32)
    return static_cast<void *>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void *dl_sym(void *handle, const char *symbol) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), symbol));
#else
    return dlsym(handle, symbol);
#endif
}

void dl_close(void *handle) {
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
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0,
        reinterpret_cast<LPSTR>(&buf), 0, nullptr
    );
    std::string msg(buf, len);
    LocalFree(buf);
    return msg;
#else
    const char *err = dlerror();
    return err ? std::string(err) : std::string{};
#endif
}

} // anonymous namespace

// ====================================================================
// Lifecycle
// ====================================================================

AlgorithmLoader::~AlgorithmLoader() { unload_all(); }

// ====================================================================
// Built-in registration
// ====================================================================

#ifdef BESQ_HAVE_HAMMING
#include "algorithm/strategies/hamming/HammingAlgorithm.h"
#endif
#ifdef BESQ_HAVE_GREEDY
#include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DFS
#include "algorithm/strategies/dfs/DFSAlgorithm.h"
#endif
#ifdef BESQ_HAVE_ASTAR
#include "algorithm/strategies/astar/AStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_IDASTAR
#include "algorithm/strategies/idastar/IDAStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
#include "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h"
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
#include "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
#include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"
#endif

void AlgorithmLoader::load_builtin() {
    if (_builtin_loaded)
        return;
    _builtin_loaded = true;

#ifdef BESQ_HAVE_HAMMING
    _registry.register_algorithm("hamming", [] { return std::make_unique<HammingAlgorithm>(); });
#endif
    #ifdef BESQ_HAVE_GREEDY
        _registry.register_algorithm("greedy",
            [] { return std::make_unique<GreedyAlgorithm>(); });
    #endif
    #ifdef BESQ_HAVE_DFS
        _registry.register_algorithm("dfs",
            [] { return std::make_unique<DFSAlgorithm>(); });
    #endif
    #ifdef BESQ_HAVE_ASTAR
        _registry.register_algorithm("astar",
            [] { return std::make_unique<AStarAlgorithm>(); });
    #endif
    #ifdef BESQ_HAVE_IDASTAR
        _registry.register_algorithm("idastar",
            [] { return std::make_unique<IDAStarAlgorithm>(); });
    #endif
    #ifdef BESQ_HAVE_HIERARCHICAL
        _registry.register_algorithm("hierarchical",
            [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
    #endif
    #ifdef BESQ_HAVE_PENALTY_BALANCE
        _registry.register_algorithm("penalty_balance",
            [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
    #endif
    #ifdef BESQ_HAVE_DIFF_FIRST
        _registry.register_algorithm("diff_first",
            [] { return std::make_unique<DiffFirstAlgorithm>(); });
    #endif

    LOG_INFO("Registered %zu built-in algorithm strategy/ies", _registry.size());
}

// ====================================================================
// Plugin loading
// ====================================================================

constexpr const char *SO_EXT =
#if defined(_WIN32)
    ".dll";
#elif defined(__APPLE__)
    ".dylib";
#else
    ".so";
#endif

size_t AlgorithmLoader::scan_and_load(const std::string &dir_path) {
    namespace fs = std::filesystem;

    if (!fs::is_directory(dir_path)) {
        LOG_WARN("Algorithms directory not found: %s", dir_path.c_str());
        return 0;
    }

    size_t count = 0;
    for (const auto &entry : fs::directory_iterator(dir_path)) {
        if (!entry.is_regular_file())
            continue;
        const auto &path = entry.path().string();
        if (!path.ends_with(SO_EXT))
            continue;
        if (load_plugin(path))
            ++count;
    }

    LOG_INFO("Loaded %zu algorithm plugin(s) from %s", count, dir_path.c_str());
    return count;
}

bool AlgorithmLoader::load_plugin(const std::string &so_path) {
    // Avoid double-load on exact path
    auto resolved = std::filesystem::weakly_canonical(so_path).string();
    for (const auto &p : _plugins) {
        if (p.path == resolved) {
            LOG_WARN("Algorithm plugin already loaded: %s", so_path.c_str());
            return true;
        }
    }

    void *handle = dl_open(so_path);
    if (!handle) {
        LOG_WARN("Failed to load algorithm plugin '%s': %s", so_path.c_str(), dl_error().c_str());
        return false;
    }

    BesqCreateFn create_fn = nullptr;
    if (!resolve_plugin(handle, so_path, create_fn)) {
        dl_close(handle);
        return false;
    }

    // Instantiate once to get the algorithm name via its virtual method.
    // This also validates that the plugin works with our besq-core.
    std::unique_ptr<IAlgorithm> probe(static_cast<IAlgorithm *>(create_fn()));
    if (!probe) {
        LOG_WARN("Plugin '%s' returned null from create", so_path.c_str());
        dl_close(handle);
        return false;
    }
    std::string algo_name(probe->name());

    // If a previously-loaded plugin has the same name, replace it.
    _registry.unregister_algorithm(algo_name);

    // Register factory
    _registry.register_algorithm(algo_name, [create_fn]() -> std::unique_ptr<IAlgorithm> {
        void *raw = create_fn();
        if (!raw)
            return nullptr;
        return std::unique_ptr<IAlgorithm>(static_cast<IAlgorithm *>(raw));
    });

    LoadedPlugin plugin;
    plugin.handle = handle;
    plugin.name   = algo_name;
    plugin.path   = std::move(resolved);
    _plugins.push_back(std::move(plugin));

    LOG_INFO("Loaded algorithm plugin: %s (from %s)", algo_name.c_str(), so_path.c_str());
    return true;
}

// ====================================================================
// Symbol resolution — looks for one symbol: besq_create_algorithm
// ====================================================================

bool AlgorithmLoader::resolve_plugin(void *handle, const std::string &path, BesqCreateFn &out_create) {
    auto create_fn = reinterpret_cast<BesqCreateFn>(dl_sym(handle, BESQ_PLUGIN_CREATE_SYM));
    if (!create_fn) {
        LOG_WARN("Plugin '%s' missing '%s': %s", path.c_str(), BESQ_PLUGIN_CREATE_SYM, dl_error().c_str());
        return false;
    }

    out_create = create_fn;
    return true;
}

// ====================================================================
// Query — delegate to internal registry
// ====================================================================

std::vector<std::string> AlgorithmLoader::list() const { return _registry.list(); }

bool AlgorithmLoader::contains(std::string_view name) const { return _registry.contains(name); }

size_t AlgorithmLoader::size() const { return _registry.size(); }

std::unique_ptr<IAlgorithm> AlgorithmLoader::create(std::string_view name) const {
    return _registry.create(name);
}

// ====================================================================
// Unload
// ====================================================================

void AlgorithmLoader::unload(const std::string &name) {
    auto it = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &p) { return p.name == name; });
    if (it == _plugins.end())
        return;

    _registry.unregister_algorithm(name);
    if (it->handle)
        dl_close(it->handle);
    _plugins.erase(it);

    LOG_INFO("Unloaded algorithm plugin: %s", name.c_str());
}

void AlgorithmLoader::unload_all() {
    for (auto &p : _plugins) {
        _registry.unregister_algorithm(p.name);
        if (p.handle)
            dl_close(p.handle);
    }
    _plugins.clear();
}
