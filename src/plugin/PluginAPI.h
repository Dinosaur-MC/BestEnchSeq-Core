#pragma once

/// @file algorithm/PluginAPI.h
/// C ABI contract between the BestEnchSeq host and algorithm plugin shared
/// libraries.  Every plugin shared library MUST export these four symbols.
///
/// Usage (inside a plugin .cpp):
///
///   #include "algorithm/PluginAPI.h"
///   #include "algorithm/strategies/greedy/GreedyAlgorithm.h"
///
///   extern "C" {
///       const char* BESQ_PLUGIN_NAME  = "greedy";
///       const char* BESQ_PLUGIN_VERSION = "1.0.0";
///
///       BESQ_PLUGIN_ENTRY(CreateAlgorithm) {
///           return new GreedyAlgorithm();
///       }
///       BESQ_PLUGIN_ENTRY(DestroyAlgorithm) {
///           delete static_cast<IAlgorithm*>(ptr);
///       }
///   }
///
/// On Linux the host must be linked with -rdynamic so that symbols from
/// the main executable / shared libraries are visible to plugins loaded
/// via dlopen.
///
/// On Windows the host and plugins must link against the same besq-core
/// DLL import library, or use the /DELAYLOAD mechanism.

#include <cstddef>

// ─── Symbol names (exported from every plugin) ─────────────────────────

#define BESQ_PLUGIN_NAME_SYM       "besq_plugin_name"
#define BESQ_PLUGIN_VERSION_SYM    "besq_plugin_version"
#define BESQ_PLUGIN_CREATE_SYM     "besq_plugin_create_algorithm"
#define BESQ_PLUGIN_DESTROY_SYM    "besq_plugin_destroy_algorithm"

// ─── Helper macro to declare a plugin export ───────────────────────────

#if defined(_WIN32) && !defined(__GNUC__)
#  define BESQ_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define BESQ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define BESQ_PLUGIN_ENTRY(name) \
    BESQ_PLUGIN_EXPORT void name(void* algo_or_null = nullptr)

// ─── Typedefs for the host-side function pointers ──────────────────────

extern "C" {

/// Returns a static C string naming the algorithm (e.g. "greedy").
typedef const char* (*BesqPluginNameFn)();

/// Returns a static C string with the plugin version (e.g. "1.0.0").
typedef const char* (*BesqPluginVersionFn)();

/// Creates a new IAlgorithm instance.  Returns an opaque pointer whose
/// lifetime is owned by the caller.  The pointer MUST have been allocated
/// with the same heap used by the host (i.e. the default operator new).
typedef void* (*BesqPluginCreateFn)();

/// Destroys an IAlgorithm instance previously returned by CreateAlgorithm.
/// `ptr` may be null (call is a no-op in that case).
typedef void (*BesqPluginDestroyFn)(void* ptr);

/// Descriptor populated when a plugin is loaded.
struct PluginDescriptor {
    const char* name{nullptr};
    const char* version{nullptr};
    void*       handle{nullptr};          // dlopen / LoadLibrary handle
    BesqPluginCreateFn  create{nullptr};
    BesqPluginDestroyFn destroy{nullptr};
};

} // extern "C"
