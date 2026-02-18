#pragma once
#include "common.h"
#include <string>
#include <unordered_set>

struct Ench {
    const int32_t id; // A nonnegative number
    int32_t level;    // A positive number

  public:
    struct Hash {
        size_t operator()(const Ench &e) const { return std::hash<int32_t>()(e.id); }
    };

    bool operator==(const Ench &other) const;

    // 构造函数
    Ench(int32_t id);
    Ench(int32_t id, int32_t level);

    // 成员方法
    std::string get_name() const;
    MCE get_supported_platform() const;
    int32_t get_max_level() const;
    int32_t get_multiplier(int32_t index) const;
    const std::unordered_set<int32_t> &get_incompatible() const;

    bool is_incompatible(const Ench &other) const;
};
