#pragma once
#include "common/CommonTypes.h"
#include "common/serialization/IBinarySerializable.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace algorithm {

// ─── Input payload — tagged by AlgorithmMode (target design, .temp/draft.md) ──
// Direct mode: current enchantments on the equipment.
// Inventory mode: the full available item set (books + equipment, any order —
//   the algorithm selects its own base equipment via Item::type) + priorities.
struct DirectPayload {
    EnchCollection source;
};
struct InventoryPayload {
    ItemCollection available;
    std::vector<int32_t> priorities;  // priority per item (lower = preferred)
};
using Payload = std::variant<DirectPayload, InventoryPayload>;

// ─── Algorithm input ───
struct AlgorithmInput : IBinarySerializable {
    EnchReg registry;          // compact registry (must be initialized)
    Payload data;              // input payload (direct / inventory)
    Item target;               // target item with wanted enchantments
    AlgorithmConfig config;    // mode + forge + search configuration

    bool is_direct() const noexcept { return config.mode == AlgorithmMode::direct; }
    bool is_inventory() const noexcept { return config.mode == AlgorithmMode::inventory; }

    const DirectPayload &direct() const noexcept { return std::get<DirectPayload>(data); }
    const InventoryPayload &inventory() const noexcept { return std::get<InventoryPayload>(data); }
    const EnchCollection &source() const noexcept { return std::get<DirectPayload>(data).source; }
    EnchCollection &source() noexcept { return std::get<DirectPayload>(data).source; }
    const ItemCollection &available() const noexcept { return std::get<InventoryPayload>(data).available; }
    ItemCollection &available() noexcept { return std::get<InventoryPayload>(data).available; }

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << registry << config << target;
        std::visit([&](const auto &p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, DirectPayload>)
                w << static_cast<uint8_t>(0) << p.source;
            else
                w << static_cast<uint8_t>(1) << p.available << p.priorities;
        }, data);
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        uint8_t disc;
        r >> registry >> config >> target >> disc;
        if (disc == 0) {
            DirectPayload p;
            r >> p.source;
            data = std::move(p);
        } else {
            InventoryPayload p;
            r >> p.available >> p.priorities;
            data = std::move(p);
        }
    }
};

// ─── Algorithm output (compact solutions) ───
struct AlgorithmOutput {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time{0};
    std::vector<EnchSolution> solutions;
    size_t task_id{0};
    Item final_item;
    bool is_valid = false;
};

}; // namespace algorithm
