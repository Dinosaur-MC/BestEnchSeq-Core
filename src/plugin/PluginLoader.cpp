#include "plugin/PluginLoader.h"
#include "log/log.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>

// ─── Platform abstraction ──────────────────────────────────────────────

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <dirent.h>
#  include <sys/stat.h>
#endif

namespace {

// ─── Shared library extension ──────────────────────────────────────────

constexpr const char* SO_EXT =
#if defined(_WIN32)
    ".dll";
#elif defined(__APPLE__)
    ".dylib";
#else
    ".so";
#endif

// ─── Helpers ───────────────────────────────────────────────────────────

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
// PluginLoader
// ====================================================================

PluginLoader& PluginLoader::instance() {
    static PluginLoader inst;
    return inst;
}

PluginLoader::~PluginLoader() {
    unload_all();
    delete _mtx;
}

// ─── Symbol resolution ─────────────────────────────────────────────────

PluginDescriptor PluginLoader::resolve_symbols(void* handle,
                                                const std::string& path) {
    PluginDescriptor desc;
    desc.handle = handle;

    // Resolve name
    auto name_fn = reinterpret_cast<BesqPluginNameFn>(
        dl_sym(handle, BESQ_PLUGIN_NAME_SYM));
    if (!name_fn) {
        LOG_WARN("Plugin '%s' missing symbol '%s': %s",
                 path.c_str(), BESQ_PLUGIN_NAME_SYM, dl_error().c_str());
        return {};
    }
    desc.name = name_fn();

    // Resolve version (optional)
    auto ver_fn = reinterpret_cast<BesqPluginVersionFn>(
        dl_sym(handle, BESQ_PLUGIN_VERSION_SYM));
    desc.version = ver_fn ? ver_fn() : "0.0.0";

    // Resolve create
    auto create_fn = reinterpret_cast<BesqPluginCreateFn>(
        dl_sym(handle, BESQ_PLUGIN_CREATE_SYM));
    if (!create_fn) {
        LOG_WARN("Plugin '%s' missing symbol '%s': %s",
                 path.c_str(), BESQ_PLUGIN_CREATE_SYM, dl_error().c_str());
        return {};
    }
    desc.create = create_fn;

    // Resolve destroy (optional — null is OK, memory will leak on unload)
    desc.destroy = reinterpret_cast<BesqPluginDestroyFn>(
        dl_sym(handle, BESQ_PLUGIN_DESTROY_SYM));

    return desc;
}

// ─── Loading ───────────────────────────────────────────────────────────

size_t PluginLoader::load_directory(const std::string& dir_path) {
    namespace fs = std::filesystem;

    if (!fs::is_directory(dir_path)) {
        LOG_WARN("Plugin directory not found: %s", dir_path.c_str());
        return 0;
    }

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path().string();
        if (!has_suffix(path, SO_EXT)) continue;
        if (load_plugin(path)) ++count;
    }

    LOG_INFO("Loaded %zu plugin(s) from %s", count, dir_path.c_str());
    return count;
}

const PluginDescriptor* PluginLoader::load_plugin(const std::string& so_path) {
    // Avoid double-load
    for (const auto& p : _plugins) {
        if (p.desc.name && p.desc.name == so_path) {
            LOG_WARN("Plugin already loaded: %s", so_path.c_str());
            return &p.desc;
        }
    }

    void* handle = dl_open(so_path);
    if (!handle) {
        LOG_WARN("Failed to load plugin '%s': %s",
                 so_path.c_str(), dl_error().c_str());
        return nullptr;
    }

    PluginDescriptor desc = resolve_symbols(handle, so_path);
    if (!desc.name || !desc.create) {
        LOG_WARN("Plugin '%s' does not expose required symbols, skipping",
                 so_path.c_str());
        dl_close(handle);
        return nullptr;
    }

    LoadedPlugin lp;
    lp.handle = handle;
    lp.desc   = desc;
    _plugins.push_back(std::move(lp));

    LOG_INFO("Loaded algorithm plugin: %s v%s (from %s)",
             desc.name, desc.version ? desc.version : "?", so_path.c_str());
    return &_plugins.back().desc;
}

// ─── Query ─────────────────────────────────────────────────────────────

std::vector<const PluginDescriptor*> PluginLoader::loaded_plugins() const {
    std::vector<const PluginDescriptor*> result;
    result.reserve(_plugins.size());
    for (const auto& p : _plugins)
        result.push_back(&p.desc);
    return result;
}

// ─── Unload ────────────────────────────────────────────────────────────

void PluginLoader::unload_plugin(const std::string& so_path) {
    auto it = std::find_if(_plugins.begin(), _plugins.end(),
        [&](const LoadedPlugin& p) { return p.desc.name == so_path; });
    if (it == _plugins.end()) return;

    if (it->handle) dl_close(it->handle);
    _plugins.erase(it);
}

void PluginLoader::unload_all() {
    for (auto& p : _plugins) {
        if (p.handle) dl_close(p.handle);
    }
    _plugins.clear();
}
