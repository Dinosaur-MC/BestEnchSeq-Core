#pragma once

/// @file registries/AlgorithmRegistration.h
/// Built-in algorithm factory.
///
/// Each compiled-in strategy is guarded by a BESQ_HAVE_* define so that
/// minimal builds register only what they link.
///
/// Available defines (set by CMake per library target):
///   BESQ_HAVE_GREEDY, BESQ_HAVE_DFS, BESQ_HAVE_ASTAR, BESQ_HAVE_IDASTAR,
///   BESQ_HAVE_HAMMING, BESQ_HAVE_HIERARCHICAL, BESQ_HAVE_PENALTY_BALANCE,
///   BESQ_HAVE_DIFF_FIRST

#include <memory>
#include <string>

class IAlgorithm;

/// Create a built-in algorithm by name.
/// The set of available strategies depends on compile-time defines.
/// Throws std::runtime_error if the name is unknown.
std::unique_ptr<IAlgorithm> create_builtin_algorithm(const std::string& name);
