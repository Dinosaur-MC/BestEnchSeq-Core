#pragma once
#include "domain/business/types/Solution.h"
#include <string>
#include <vector>

struct SolveResult {
    bool success = false;
    std::vector<Solution> solutions;
    std::string algorithm_used;
    int64_t computation_time_ms = 0;
};
