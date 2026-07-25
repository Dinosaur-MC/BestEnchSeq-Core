#pragma once
#include "domain/orchestration/types/ExportRequest.h"
#include "domain/orchestration/types/ExportResult.h"

class Profile;

struct ExportPipeline {
    static ExportResult run(
        const Profile& profile,
        const ExportRequest& request
    );
};
