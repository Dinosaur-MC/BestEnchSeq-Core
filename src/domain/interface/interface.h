#pragma once

// ====================================================================
// BestEnchSeq — Interface Domain
// ====================================================================
// Translation layer: external input → domain types → orchestration.
// Stateless at the module level; BesqContext owns per-session state.
//
// Exported modules:
//   CLI  — CLI::Config, CLI::parse(), CLI::help_text()
//   ABI  — C ABI bindings (CAbiBindings.cpp, no public header)

#include "domain/interface/cli/CLIApp.h"  // IWYU pragma: export
