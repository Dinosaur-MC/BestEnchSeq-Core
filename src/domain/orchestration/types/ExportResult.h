#pragma once
#include <string>

struct ExportResult {
    bool success = false;
    std::string output_path;
    std::string content;
};
