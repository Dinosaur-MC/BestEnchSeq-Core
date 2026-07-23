#pragma once

// ====================================================================
// BestEnchSeq — Interface Domain
// ====================================================================
// CLI, public API, parsers, and intermediate data types for external
// consumers (end users, tests, higher-level tools).

// ── Public API ──
#include "domain/interface/api/SolvePipeline.h"   // IWYU pragma: export
#include "domain/interface/api/ProfileSet.h"      // IWYU pragma: export

// ── CLI Layer ──
#include "domain/interface/cli/cli.h"             // IWYU pragma: export
#include "domain/interface/cli/RegistryEditor.h"  // IWYU pragma: export

// ── Parsers ──
#include "domain/interface/parsers/CLIParser.h"           // IWYU pragma: export
#include "domain/interface/parsers/EnchInfoParser.h"      // IWYU pragma: export
#include "domain/interface/parsers/EnchParser.h"          // IWYU pragma: export
#include "domain/interface/parsers/ItemParser.h"          // IWYU pragma: export
#include "domain/interface/parsers/ParserUtilsDomain.hpp" // IWYU pragma: export

// ── Raw (Pre-Resolution) Types ──
#include "domain/interface/types/RawTypes.h"      // IWYU pragma: export

// ── Interface Components ──
#include "domain/interface/components/TagResolver.hpp"    // IWYU pragma: export
