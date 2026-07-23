#pragma once

// ====================================================================
// BestEnchSeq — Interface Domain
// ====================================================================
// CLI, public API, parsers, and intermediate data types for external
// consumers (end users, tests, higher-level tools).

// ── Public API ──
#include "api/SolvePipeline.h"   // SolvePipeline, SolveInput, SolveResult
#include "api/ProfileSet.h"      // Profile / ProfileSet management

// ── CLI Layer ──
#include "cli/cli.h"             // CLIConfig, parse_cli(), build_target(), build_enchset()
#include "cli/RegistryEditor.h"  // apply_registry_edits()

// ── Parsers ──
#include "parsers/CLIParser.h"           // Generic --key=value argument parser
#include "parsers/EnchInfoParser.h"      // Enchantment/equipment data file parser
#include "parsers/EnchParser.h"          // Enchantment spec string parser
#include "parsers/ItemParser.h"          // Item spec string parser
#include "parsers/ParserUtilsDomain.hpp" // Parser utility templates

// ── Raw (Pre-Resolution) Types ──
#include "types/RawTypes.h"      // RawEnchantment, RawEquipment, Id

// ── Interface Components ──
#include "components/TagResolver.hpp"    // MC tag file loader and resolver
