#pragma once

/// @file loader/PluginEntry.h
/// One-liner convenience macro for algorithm plugin authors.
///
///   BESQ_PLUGIN_ENTRY(GreedyAlgorithm)               // defaults to None
///   BESQ_PLUGIN_ENTRY_CAP(MyAlgo, PluginCapability::None)
///
/// Expands to:
///   extern "C" {
///       BESQ_PLUGIN_EXPORT void* besq_create_algorithm() {
///           return new GreedyAlgorithm();
///       }
///       BESQ_PLUGIN_EXPORT PluginCapability besq_plugin_capability() {
///           return PluginCapability::None;
///       }
///   }

#include "PluginAPI.h"

/// Declare a plugin entry point with an explicit capability manifest.
/// PluginCapability::None is the right choice for pure-compute plugins.
#define BESQ_PLUGIN_ENTRY_CAP(AlgoClass, CapabilityLevel) \
    extern "C" { \
        BESQ_PLUGIN_EXPORT void* besq_create_algorithm() { \
            return new AlgoClass(); \
        } \
        BESQ_PLUGIN_EXPORT PluginCapability besq_plugin_capability() { \
            return CapabilityLevel; \
        } \
    }

/// Shorthand — capability defaults to PluginCapability::None
/// (pure computation, no filesystem/network).
#define BESQ_PLUGIN_ENTRY(AlgoClass) \
    BESQ_PLUGIN_ENTRY_CAP(AlgoClass, PluginCapability::None)
