#include "loader/AlgorithmLoader.h"
#include "algorithm/IAlgorithm.h"
#include "log/log.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>

// ─── Platform dlopen/dlsym abstraction ────────────────────────────────

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
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
    return reinterpret_cast<void*>(GetProcAddress(
        static_cast<HMODULE>(handle), symbol));
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
    if (err == 0) return {};
    LPSTR buf = nullptr;
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr, err, 0, reinterpret_cast<LPSTR>(&buf), 0, nullptr);
    std::string msg(buf, len);
    LocalFree(buf);
    return msg;
#else
    const char* err = dlerror();
    return err ? std::string(err) : std::string{};
#endif
}

bool has_suffix(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // anonymous namespace

// ====================================================================
// Lifecycle
// ====================================================================

AlgorithmLoader::~AlgorithmLoader() {
    unload_all();
}

// ====================================================================
// Built-in registration
// ====================================================================

// Each strategy is guarded by a compile-time define so that minimal
// builds register only what they link.

#ifdef BESQ_HAVE_HAMMING
#  include "algorithm/strategies/hamming/HammingAlgorithm.h"
#endif
#ifdef BESQ_HAVE_GREEDY
#  include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DFS
#  include "algorithm/strategies/dfs/DFSAlgorithm.h"
#endif
#ifdef BESQ_HAVE_ASTAR
#  include "algorithm/strategies/astar/AStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_IDASTAR
#  include "algorithm/strategies/idastar/IDAStarAlgorithm.h"
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
#  include "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h"
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
#  include "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
#  include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"
#endif

void AlgorithmLoader::load_builtin() {
    if (_builtin_loaded) return;
    _builtin_loaded = true;

#ifdef BESQ_HAVE_HAMMING
    _registry.register_algorithm("hamming",
        [] { return std::make_unique<HammingAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_GREEDY
    // greedy — simple greedy search, near-optimal for most cases
    _registry.register_algorithm("greedy",
        [] { return std::make_unique<GreedyAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_DFS
    // dfs — brute-force depth-first search, simplest possible
    _registry.register_algorithm("dfs",
        [] { return std::make_unique<DFSAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_ASTAR
    // astar — optimal A* with search budget control
    _registry.register_algorithm("astar",
        [] { return std::make_unique<AStarAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_IDASTAR
    // idastar — iterative-deepening A*, memory-efficient optimal search
    _registry.register_algorithm("idastar",
        [] { return std::make_unique<IDAStarAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_HIERARCHICAL
    // hierarchical — merges intermediate results via hierarchical planning
    _registry.register_algorithm("hierarchical",
        [] { return std::make_unique<HierarchicalMergeAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_PENALTY_BALANCE
    // penalty_balance — dynamic penalty-balancing search
    _registry.register_algorithm("penalty_balance",
        [] { return std::make_unique<DynamicPenaltyBalancingAlgorithm>(); });
#endif
#ifdef BESQ_HAVE_DIFF_FIRST
    // diff_first — difference-first heuristic search
    _registry.register_algorithm("diff_first",
        [] { return std::make_unique<DiffFirstAlgorithm>(); });
#endif

    LOG_INFO("Registered %zu built-in algorithm strategy/ies", _registry.size());
}

// ====================================================================
// Plugin loading — shared library discovery
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
        LOG_WARN("Algorithms directory not found: %s", dir_path.c_str());
        return 0;
    }

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path().string();
        if (!has_suffix(path, SO_EXT)) continue;
        if (load_plugin(path)) ++count;
    }

    LOG_INFO("Loaded %zu algorithm plugin(s) from %s", count, dir_path.c_str());
    return count;
}

bool AlgorithmLoader::load_plugin(const std::string& so_path) {
    // Avoid double-load on exact path
    for (const auto& p : _plugins) {
        if (p->path == so_path) {
            LOG_WARN("Algorithm plugin already loaded: %s", so_path.c_str());
            return true;
        }
    }

    void* handle = dl_open(so_path);
    if (!handle) {
        LOG_WARN("Failed to load algorithm plugin '%s': %s",
                 so_path.c_str(), dl_error().c_str());
        return false;
    }

    auto desc = resolve_symbols(handle, so_path);
    if (!desc.name || !desc.create) {
        LOG_WARN("File '%s' is not a valid algorithm plugin (missing symbols), skipping",
                 so_path.c_str());
        dl_close(handle);
        return false;
    }

    // If a built-in or previously-loaded plugin has the same name, the
    // plugin takes precedence (replaces).
    _registry.unregister_algorithm(desc.name);

    // Wrap the C ABI factory into an AlgorithmRegistry-compatible functor.
    // The ABI guarantees the same heap is used, so plain delete is safe.
    auto create_fn = desc.create;
    _registry.register_algorithm(desc.name,
        [create_fn]() -> std::unique_ptr<IAlgorithm> {
            void* raw = create_fn();
            if (!raw) return nullptr;
            return std::unique_ptr<IAlgorithm>(
                static_cast<IAlgorithm*>(raw));
        });

    // Track the loaded plugin for unload.
    auto plugin = std::make_unique<LoadedPlugin>();
    plugin->handle  = handle;
    plugin->name    = desc.name;
    plugin->path    = so_path;
    plugin->create  = desc.create;
    plugin->destroy = desc.destroy;
    _plugins.push_back(std::move(plugin));

    LOG_INFO("Loaded algorithm plugin: %s v%s (from %s)",
             desc.name, desc.version ? desc.version : "?",
             so_path.c_str());
    return true;
}

// ====================================================================
// Symbol resolution
// ====================================================================

BesqPluginDescriptor AlgorithmLoader::resolve_symbols(
    void* handle, const std::string& path)
{
    BesqPluginDescriptor desc;
    desc.handle = handle;

    // Name (required)
    auto name_fn = reinterpret_cast<BesqAlgorithmNameFn>(
        dl_sym(handle, "besq_algorithm_name"));
    if (!name_fn) {
        LOG_WARN("Plugin '%s' missing 'besq_algorithm_name': %s",
                 path.c_str(), dl_error().c_str());
        return {};
    }
    desc.name = name_fn();

    // Version (optional)
    auto ver_fn = reinterpret_cast<BesqAlgorithmVersionFn>(
        dl_sym(handle, "besq_algorithm_version"));
    desc.version = ver_fn ? ver_fn() : "0.0.0";

    // Create (required)
    auto create_fn = reinterpret_cast<BesqAlgorithmCreateFn>(
        dl_sym(handle, "besq_algorithm_create"));
    if (!create_fn) {
        LOG_WARN("Plugin '%s' missing 'besq_algorithm_create': %s",
                 path.c_str(), dl_error().c_str());
        return {};
    }
    desc.create = create_fn;

    // Destroy (optional — the host uses delete on the same heap)
    desc.destroy = reinterpret_cast<BesqAlgorithmDestroyFn>(
        dl_sym(handle, "besq_algorithm_destroy"));

    return desc;
}

// ====================================================================
// Query — delegate to internal registry
// ====================================================================

std::vector<std::string> AlgorithmLoader::list() const {
    return _registry.list();
}

bool AlgorithmLoader::contains(std::string_view name) const {
    return _registry.contains(name);
}

size_t AlgorithmLoader::size() const {
    return _registry.size();
}

std::unique_ptr<IAlgorithm> AlgorithmLoader::create(std::string_view name) const {
    return _registry.create(name);
}

// ====================================================================
// Unload
// ====================================================================

void AlgorithmLoader::unload(const std::string& name) {
    auto it = std::find_if(_plugins.begin(), _plugins.end(),
        [&](const auto& p) { return p->name == name; });
    if (it == _plugins.end()) return;

    _registry.unregister_algorithm(name);

    if ((*it)->handle) dl_close((*it)->handle);
    _plugins.erase(it);

    LOG_INFO("Unloaded algorithm plugin: %s", name.c_str());
}

void AlgorithmLoader::unload_all() {
    for (auto& p : _plugins) {
        _registry.unregister_algorithm(p->name);
        if (p->handle) dl_close(p->handle);
    }
    _plugins.clear();
}

// ====================================================================
// Utility: default algorithms directory
// ====================================================================

std::string AlgorithmLoader::default_algorithms_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return "algorithms";
    std::string exe_path(buf, len);
#else
    // Linux: /proc/self/exe
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "algorithms";
    buf[len] = '\0';
    std::string exe_path(buf);
#endif

    auto dir = std::filesystem::path(exe_path).parent_path();
    return (dir / "algorithms").string();
}
