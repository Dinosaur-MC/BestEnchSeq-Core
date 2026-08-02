#pragma once
#include "domain/orchestration/types/ExportRequest.h"
#include "domain/orchestration/types/ExportResult.h"

class Profile;

struct ExportPipeline {
    static ExportResult run(
        const Profile& profile,
        const ExportRequest& request
    );

    /// Infer the export format from a file path extension (`.csv`/`.CSV` →
    /// Csv, everything else → Json).  Shared single source of truth — the
    /// CLI and C ABI reach it transitively via BesqContext::export_registry,
    /// which calls this helper directly.
    static ExportRequest::Format format_for_path(const std::string& path);
};
