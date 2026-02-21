#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common.h"

struct EnchInfo {
    const std::string name;                              // Unique id
    const MCE supported_platform;                        // Supported platform
    const int32_t max_level;                             // Maximum level
    const int32_t limited_level;                         // 0 <= Limmited level <= maximum level
    const int32_t multiplier;                            // Positive integer
    const std::unordered_set<std::string> exclusive_set; // Incompatible enchantments (by name)

  private:
    static std::vector<EnchInfo> instances;                        // All instances
    static std::unordered_map<std::string, int32_t> name_to_index; // Name to index mapping
    static std::unordered_map<int32_t, std::unordered_set<int32_t>>
        incompatible_table; // Incompatible table (by index)

    static MCE active_platform; // Active platform

    static bool check_validation(const std::vector<EnchInfo> &infos);

  public:
    bool operator==(const EnchInfo &other) const;

    // Hash function for unordered_map
    struct Hash {
        size_t operator()(const EnchInfo &info) const { return std::hash<std::string>()(info.name); }
    };

    static void initialize(const std::vector<EnchInfo> &infos);
    static const std::vector<EnchInfo> &get_instances();

    static const EnchInfo &get(int32_t index);
    static const EnchInfo &get(const std::string &name);
    static int32_t get_id(const std::string &name);

    static void set_active_platform(MCE type);
    static MCE get_active_platform();

    static const std::unordered_set<int32_t> &get_incompatible(int32_t e);
    static bool is_incompatible(int32_t e1, int32_t e2);
};

using EnchInfoList = std::vector<EnchInfo>;
