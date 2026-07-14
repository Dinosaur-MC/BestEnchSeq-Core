#pragma once
#include "algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "algorithm/diagnostics/AStarDiagnostics.h"

/// Free-function diagnostics writers, separated from the pure-data
/// diagnostics structs.
///
/// Compiled out when BESQ_DISABLE_DIAGNOSTICS is defined.
namespace DiagnosticsWriter {

#ifndef BESQ_DISABLE_DIAGNOSTICS
void write(const AlgorithmDiagnostics& diag);
void write(const AStarDiagnostics& diag);
#else
inline void write(const AlgorithmDiagnostics&) {}
inline void write(const AStarDiagnostics&) {}
#endif

} // namespace DiagnosticsWriter
