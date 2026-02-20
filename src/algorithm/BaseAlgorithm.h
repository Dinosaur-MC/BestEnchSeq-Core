#pragma once
#include "../BESQTypes.h"
#include <algorithm> // IWYU pragma: export
#include <atomic>

class BaseAlgorithm {
  public:
    enum State {
        None = 0,
        Ready,
        Running,
        Finished,
    };

    template <class ExtraConfig> struct _Config {
        bool ignore_penalty_cost;
        bool ignore_repair_cost;
        ExtraConfig get_extra_config();
    };
    using Config = _Config<void>;

    struct Input {
        EnchSet original_ench;
        ItemStack target_item;
        ItemCollection available_items;
    };

    struct Output {
        std::string algorithm_name;
        std::string algorithm_version;
        size_t created_at;
        size_t computation_time;
        ForgeOrder order;
        bool is_valid;
    };

  protected:
    std::atomic<State> _state;
    Config _config;
    Input _input;
    Output _output;

  public:
    BaseAlgorithm()                               = default;
    virtual ~BaseAlgorithm()                      = default;
    virtual void initialize(const Config &config) = 0;
    virtual void run(const Input &input)          = 0;
    virtual void stop()                           = 0;
    State get_state() const;
    Output get_output() const;

    virtual std::pair<ItemStack, int32_t>
    forge_item(const ItemStack &item_a, const ItemStack &item_b, bool updated = false) const;
};

int32_t calc_exp(int32_t level);
