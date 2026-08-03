#pragma once

/// @file plugin/PluginAudit.h
/// Static binary analysis for plugin shared libraries.
///
/// Scans a .so / .dll *before* dlopen to detect:
///   - W^X violations (write+exec segment → hard reject)
///   - Unexpected exports (plugin exposing extra symbols)
///   - Dangerous imports (network, process execution, dynamic code loading)
///   - Linked libraries (DT_NEEDED / import DLLs)
///
/// Combined with the Capability Manifest (besq_plugin_capability symbol),
/// this gives the loader a strong signal about whether a plugin is safe
/// to load in a pure-compute context.

#include "PluginAPI.h"
#include <string>
#include <vector>

namespace algorithm {

struct PluginAuditReport {
    bool passed{true}; ///< Overall verdict.  false → should not be loaded.

    // ── Audit completeness ────────────────────────────────────
    /// True when the scanner could NOT fully inspect imports/exports (e.g. the
    /// binary has no section headers or no dynamic symbol table).  The plugin
    /// is structurally valid but OPAQUE — the loader treats this like a
    /// dangerous import: refuse without a sandbox, permit when contained.
    bool limited{false};

    // ── W^X policy ─────────────────────────────────────────────
    bool has_wx_segment{false}; ///< Segment with PF_W|PF_X → EXPLOIT

    // ── Export audit ───────────────────────────────────────────
    /// Symbols exported by the plugin other than the standard
    /// besq_create_algorithm and besq_plugin_capability.
    std::vector<std::string> extra_exports;

    // ── Import audit ───────────────────────────────────────────
    /// Network / process / dlopen symbols the plugin imports.
    std::vector<std::string> dangerous_imports;
    /// All DT_NEEDED (ELF) or import-library (PE) entries.
    std::vector<std::string> linked_libraries;

    // ── Capability manifest (filled by loader post-dlopen) ─────
    bool has_manifest{false};
    PluginCapability capability{PluginCapability::Unrestricted};
};

/// Read a shared-library binary and produce a security report.
/// No code from the library is executed — the file is mmap'd
/// and parsed structurally.
PluginAuditReport audit_plugin_binary(const std::string& so_path);

} // namespace algorithm
