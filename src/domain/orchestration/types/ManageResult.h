#pragma once
#include <string>
#include <vector>

struct ManageResult {
    bool success = true;
    std::string message;
    std::vector<std::string> profile_list;
};
