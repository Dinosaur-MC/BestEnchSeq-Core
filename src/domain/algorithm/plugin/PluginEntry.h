#pragma once

/// @file loader/PluginEntry.h
/// One-liner convenience macro for algorithm plugin authors.
///
///   BESQ_PLUGIN_ENTRY(GreedyAlgorithm)
///
/// Expands to:
///   extern "C" {
///       BESQ_PLUGIN_EXPORT void* besq_create_algorithm() {
///           return new GreedyAlgorithm();
///       }
///   }

#include "PluginAPI.h"
#include "../IAlgorithm.h"

#define BESQ_PLUGIN_ENTRY(AlgoClass) \
    extern "C" { \
        BESQ_PLUGIN_EXPORT void* besq_create_algorithm() { \
            return new AlgoClass(); \
        } \
    }
