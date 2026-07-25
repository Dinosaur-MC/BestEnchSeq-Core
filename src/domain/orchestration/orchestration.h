#pragma once

// ====================================================================
// BestEnchSeq — Orchestration Layer
// ====================================================================
// Cross-domain adapters, serializers, formatters, and pipelines.
// Bridges business/interface domain types and algorithm compact types.
// Core pipelines run independently of the interface domain.

// ── Types ──
#include "domain/orchestration/types/SolveRequest.h"    // IWYU pragma: export
#include "domain/orchestration/types/SolveResult.h"     // IWYU pragma: export
#include "domain/orchestration/types/ManageRequest.h"   // IWYU pragma: export
#include "domain/orchestration/types/ManageResult.h"    // IWYU pragma: export
#include "domain/orchestration/types/ExportRequest.h"   // IWYU pragma: export
#include "domain/orchestration/types/ExportResult.h"    // IWYU pragma: export

// ── Components ──
#include "domain/orchestration/components/CompactAdapter.h"   // IWYU pragma: export
#include "domain/orchestration/components/OutputFormatter.h"  // IWYU pragma: export
#include "domain/orchestration/components/EnchSerializer.h"   // IWYU pragma: export

// ── Pipelines ──
#include "domain/orchestration/pipelines/SolvePipeline.h"    // IWYU pragma: export
#include "domain/orchestration/pipelines/ManagePipeline.h"   // IWYU pragma: export
#include "domain/orchestration/pipelines/ExportPipeline.h"   // IWYU pragma: export
