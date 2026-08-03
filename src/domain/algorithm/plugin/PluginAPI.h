#pragma once

/// @file loader/PluginAPI.h
/// Minimal C ABI for algorithm plugin shared libraries.
///
/// Each plugin exports one symbol: besq_create_algorithm
/// which returns a fully constructed IAlgorithm*.
///
/// Since both the host and plugins link against the same besq-core
/// shared library, the IAlgorithm vtable and heap are shared — no
/// special destroy function needed.
///
/// Plugin author usage:
///   #include "loader/PluginEntry.h"
///   class MyAlgo : public IAlgorithm { ... };
///   BESQ_PLUGIN_ENTRY(MyAlgo)

#if defined(_WIN32) && !defined(__GNUC__)
#define BESQ_PLUGIN_EXPORT __declspec(dllexport)
#else
#define BESQ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define BESQ_PLUGIN_CREATE_SYM "besq_create_algorithm"

extern "C" {
/// Factory: returns a raw IAlgorithm*.
typedef void* (*BesqCreateFn)();
}
