#pragma once

/// @file loader/PluginEntry.h
/// Convenience macro for algorithm plugin authors.
///
/// One-liner that expands to the four required extern "C" exports:
///
///   BESQ_PLUGIN_ENTRY(MyAlgorithm, "my-name", "1.0.0")
///
/// Expands to:
///
///   extern "C" {
///       BESQ_PLUGIN_EXPORT const char* besq_algorithm_name    = "my-name";
///       BESQ_PLUGIN_EXPORT const char* besq_algorithm_version = "1.0.0";
///       BESQ_PLUGIN_EXPORT void*       besq_algorithm_create() { return new MyAlgorithm(); }
///       BESQ_PLUGIN_EXPORT void        besq_algorithm_destroy(void* p) {
///           delete static_cast<IAlgorithm*>(p);
///       }
///   }

#include "loader/PluginAPI.h"

#define BESQ_PLUGIN_ENTRY(AlgoClass, NameStr, VersionStr) \
    extern "C" { \
        BESQ_PLUGIN_EXPORT const char* besq_algorithm_name = NameStr; \
        BESQ_PLUGIN_EXPORT const char* besq_algorithm_version = VersionStr; \
        BESQ_PLUGIN_EXPORT void* besq_algorithm_create() { \
            return new AlgoClass(); \
        } \
        BESQ_PLUGIN_EXPORT void besq_algorithm_destroy(void* ptr) { \
            delete static_cast<IAlgorithm*>(ptr); \
        } \
    }
