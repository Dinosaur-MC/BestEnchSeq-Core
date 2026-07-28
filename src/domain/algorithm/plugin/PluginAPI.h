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
///
/// Security:
///   Plugins may optionally export besq_plugin_capability to
///   declare their required privilege level.  The host audits
///   each plugin at load time (see PluginAudit).

#include <cstdint>

#if defined(_WIN32) && !defined(__GNUC__)
#  define BESQ_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define BESQ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define BESQ_PLUGIN_CREATE_SYM      "besq_create_algorithm"
#define BESQ_PLUGIN_CAPABILITY_SYM  "besq_plugin_capability"

// ── Plugin Capability Manifest ──────────────────────────────────
// Every plugin may optionally export a capability level.
// The host verifies the declared level against the plugin's binary
// imports to detect capability overreach.
//
//   None          — 纯计算，无文件/网络/进程操作 (纯 forge 算法推荐)
//   Filesystem    — 需要读/写磁盘（如外部 config/权重文件）
//   Network       — 需要网络请求
//   Unrestricted  — 完全信任，不做限制
//
// 未导出的插件，host 视为 Unrestricted 并打印警告。
enum class PluginCapability : uint8_t {
    None          = 0,
    Filesystem    = 1,
    Network       = 2,
    Unrestricted  = 255,
};

extern "C" {
    /// Factory: returns a raw IAlgorithm*.
    typedef void* (*BesqCreateFn)();

    /// Capability query: returns the plugin's declared privilege level.
    typedef PluginCapability (*BesqCapabilityFn)();
}
