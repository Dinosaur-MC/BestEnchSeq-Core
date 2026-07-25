#pragma once

// ====================================================================
// BestEnchSeq — Interface Domain
// ====================================================================
// Translation layer: external input → domain types → orchestration.
// Stateless at the module level; BesqContext owns per-session state.

// ── CLI Module ──
#include "domain/interface/cli/cli.h"             // IWYU pragma: export
#include "domain/interface/cli/CLIParser.h"       // IWYU pragma: export
#include "domain/interface/cli/EnchParser.h"      // IWYU pragma: export
#include "domain/interface/cli/ItemParser.h"      // IWYU pragma: export
#include "domain/interface/cli/RegistryEditor.h"  // IWYU pragma: export

