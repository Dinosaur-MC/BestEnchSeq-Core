#pragma once
#include "domain/business/types/Solution.h"
#include "common/CommonTypes.h"
#include <cstdint>
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

    // ── SolveResult 元数据（Solution+Json 导出时透传到 JSON root）──
    bool success = true;                    ///< 求解是否成功（透传到 JSON root 的 success 键；非导出操作成功）
    std::string algorithm_used;             ///< 使用的算法名
    int64_t computation_time_ms = 0;        ///< 计算耗时（毫秒）
};
