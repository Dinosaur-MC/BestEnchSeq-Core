#pragma once
#include "domain/business/types/Item.h"
#include "domain/business/types/EnchSet.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "common/CommonTypes.h"
#include <string>
#include <variant>
#include <vector>

struct DirectPayload {
    EnchSet source_enchantments;
};

struct InventoryPayload {
    std::vector<Item> extra_items;
    std::vector<int32_t> extra_item_priorities;
};

using SolvePayload = std::variant<DirectPayload, InventoryPayload>;

struct SolveRequest {
    Item target_item;
    AlgorithmMode mode;
    SolvePayload payload;
    algorithm::ForgeConfig forge_config;
    algorithm::SearchConfig search_config;
    std::string algorithm = "dp_merge";
};
