#pragma once
#include "domain/business/types/Solution.h"
#include "common/CommonTypes.h"
#include <string>
#include <vector>

struct ExportRequest {
    enum class Format { Json, Verbose, Compact, Csv, McOfficial };
    enum class TargetType { Registry, Solution };

    TargetType target;
    Format format = Format::Json;
    std::string output_path;
    std::vector<Solution> solutions;
    AlgorithmMode mode = AlgorithmMode::direct;
};
