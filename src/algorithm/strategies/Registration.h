#pragma once
#include "registries/AlgorithmRegistry.h"

/// Register all built-in algorithm strategies into the registry.
/// Implemented by CMake-generated code in Registration.cpp.
void besq_register_builtin_strategies(AlgorithmRegistry &reg);
