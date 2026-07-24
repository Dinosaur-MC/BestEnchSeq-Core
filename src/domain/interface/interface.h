#pragma once

// ====================================================================
// BestEnchSeq — Interface Domain
// ====================================================================
// CLI, public API, parsers, and intermediate data types for external
// consumers (end users, tests, higher-level tools).

// ── Public API ──
#include "domain/interface/SolvePipeline.h"   // IWYU pragma: export

// ── CLI Layer ──
#include "domain/interface/cli/cli.h"             // IWYU pragma: export
#include "domain/interface/cli/RegistryEditor.h"  // IWYU pragma: export

// ── CLI Parsers ──
#include "domain/interface/parsers/CLIParser.h"           // IWYU pragma: export
#include "domain/interface/parsers/EnchParser.h"          // IWYU pragma: export
#include "domain/interface/parsers/ItemParser.h"          // IWYU pragma: export

// ── Interface Components ──
#include "domain/interface/components/ParserUtilsDomain.hpp"      // IWYU pragma: export

// ── CLI Spec Types ──
#include "domain/interface/types/SpecTypes.h"     // IWYU pragma: export

// ── File Format Detection ──
#include "domain/interface/fs/FileFormat.h"        // IWYU pragma: export
