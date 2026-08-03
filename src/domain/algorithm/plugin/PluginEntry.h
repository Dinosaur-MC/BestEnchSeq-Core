#pragma once

/// @file loader/PluginEntry.h
/// One-liner convenience macro for algorithm plugin authors.
///
///   BESQ_PLUGIN_ENTRY(MyAlgorithm)
///
/// Expands to:
///   extern "C" {
///       BESQ_PLUGIN_EXPORT void* besq_create_algorithm() {
///           return new MyAlgorithm();
///       }
///   }

#include "PluginAPI.h"

/// Declare the plugin's single required entry point.
#define BESQ_PLUGIN_ENTRY(AlgoClass)                                                                                           \
    extern "C" {                                                                                                               \
    BESQ_PLUGIN_EXPORT void* besq_create_algorithm() {                                                                         \
        return new AlgoClass();                                                                                                \
    }                                                                                                                          \
    }
