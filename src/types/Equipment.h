#pragma once
#include "EnchInfo.h"
#include "EnchSet.h"

struct Equipment {
    const std::string id;
    const std::string name;
    const int32_t max_durability;
    const std::unordered_set<std::string> applicable_enchantments;

  public:
    struct Hash {
        size_t operator()(const Equipment &equi) const { return std::hash<std::string>()(equi.id); }
    };
    bool operator==(const Equipment &other) const;

    bool is_applicable(const std::string &ench) const;
    bool is_applicable(const Ench &ench) const;

    EnchSet filter_enchantments(const EnchSet &enchantments) const;
    EnchInfoList filter_enchantments(const EnchInfoList &enchantments) const;

    static int32_t merge_durability(int32_t d1, int32_t d2, int32_t max_d);
    static int32_t repair_durability(int32_t d, int32_t n, int32_t max_d);
    int32_t calc_merge_durability(int32_t d1, int32_t d2) const;
    int32_t calc_repair_durability(int32_t d, int32_t n) const;
};
