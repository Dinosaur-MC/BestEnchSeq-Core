#pragma once
#include "domain/orchestration/types/ManageRequest.h"
#include "domain/orchestration/types/ManageResult.h"

class ProfileManager;
class ProfileLoader;

struct ManagePipeline {
    static ManageResult run(
        ProfileManager& profiles,
        ProfileLoader& loader,
        const ManageRequest& request
    );
};
