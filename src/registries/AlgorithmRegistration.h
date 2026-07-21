#pragma once

/// @file registries/AlgorithmRegistration.h
/// Built-in algorithm factory and persistent global registry management.
///
/// The global AlgorithmRegistry singleton is lazily initialised on first use
/// with all compiled-in strategies.  Plugin-loaded algorithms are appended
/// at runtime via PluginLoader.

#include <memory>
#include <string>

class AlgorithmRegistry;
class IAlgorithm;

// ─── Global registry ───────────────────────────────────────────────────

/// Returns the process-wide AlgorithmRegistry singleton.
/// Built-in algorithms are registered on the first call.
AlgorithmRegistry& global_algorithm_registry();

// ─── Built-in factory (for fallback / testing) ─────────────────────────

/// Create a built-in algorithm by name, bypassing the global registry.
/// The set of available strategies depends on compile-time defines
/// (BESQ_HAVE_GREEDY, BESQ_HAVE_DFS, …).
/// Throws std::runtime_error if the name is unknown.
std::unique_ptr<IAlgorithm> create_builtin_algorithm(const std::string& name);

/// Register all compiled-in strategies into the given registry.
void register_builtin_algorithms(AlgorithmRegistry& registry);
