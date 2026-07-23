#pragma once
#include "domain/algorithm/registries/AlgorithmRegistry.h"

/// Register all built-in algorithm strategies into the registry.
/// Implemented by CMake-generated code in _strategy_registration.cpp.
/// @param reg The algorithm registry to populate.
void besq_register_builtin_strategies(algorithm::AlgorithmRegistry &reg);
