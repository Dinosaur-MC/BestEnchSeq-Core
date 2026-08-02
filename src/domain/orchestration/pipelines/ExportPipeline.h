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
    /// Csv, everything else → Json).  Becomes the shared single source of
    /// truth for BesqContext::export_registry / CLI / C ABI once P2.3 wires
    /// BesqContext to ExportPipeline (today BesqContext still carries an
    /// inline extension check).
    static ExportRequest::Format format_for_path(const std::string& path);
};
