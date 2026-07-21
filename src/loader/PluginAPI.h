#pragma once

/// @file loader/PluginAPI.h
/// C ABI contract between the BestEnchSeq host and algorithm plugin shared
/// libraries.  Every plugin shared library MUST export these four symbols.
///
/// Plugin author usage:
///
///   #include "loader/PluginEntry.h"
///
///   class MyAlgorithm : public IAlgorithm { ... };
///   BESQ_PLUGIN_ENTRY(MyAlgorithm, "my-algorithm", "1.0.0")
///
/// Or manually (equivalent):
///
///   #include "loader/PluginAPI.h"
///   extern "C" {
///       BESQ_PLUGIN_EXPORT const char* besq_algorithm_name    = "my-algorithm";
///       BESQ_PLUGIN_EXPORT const char* besq_algorithm_version = "1.0.0";
///       BESQ_PLUGIN_EXPORT void*       besq_algorithm_create() { return new MyAlgorithm(); }
///       BESQ_PLUGIN_EXPORT void        besq_algorithm_destroy(void* p) {
///           delete static_cast<IAlgorithm*>(p);
///       }
///   }

// ─── Export visibility ────────────────────────────────────────────────

#if defined(_WIN32) && !defined(__GNUC__)
#  define BESQ_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define BESQ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// ─── Symbol names (queried by AlgorithmLoader) ────────────────────────

// These strings are the exact symbol names the loader dlsym's.
// Plugin shared libraries must export them with C linkage.
//
//   besq_algorithm_name      — static const char* (required)
//   besq_algorithm_version   — static const char* (optional, defaults to "0.0.0")
//   besq_algorithm_create    — void* (*)(void)        (required)
//   besq_algorithm_destroy   — void  (*)(void*)       (optional, memory leaks if absent)

// ─── Host-side type aliases ───────────────────────────────────────────

extern "C" {

/// Returns a static C string naming the algorithm (e.g. "greedy").
typedef const char* (*BesqAlgorithmNameFn)();

/// Returns a static C string with the plugin version (e.g. "1.0.0").
typedef const char* (*BesqAlgorithmVersionFn)();

/// Creates a new IAlgorithm instance.
/// Returns an opaque pointer whose lifetime is owned by the caller.
/// The pointer MUST have been allocated with the same heap used by
/// the host (i.e. the default operator new).
typedef void* (*BesqAlgorithmCreateFn)();

/// Destroys an IAlgorithm instance previously returned by _create.
/// `ptr` may be null (call is a no-op in that case).
typedef void (*BesqAlgorithmDestroyFn)(void* ptr);

/// Descriptor populated when a plugin is successfully loaded.
struct BesqPluginDescriptor {
    const char*            name{nullptr};
    const char*            version{nullptr};
    void*                  handle{nullptr};   // dlopen / LoadLibrary handle
    BesqAlgorithmCreateFn  create{nullptr};
    BesqAlgorithmDestroyFn destroy{nullptr};
};

} // extern "C"
