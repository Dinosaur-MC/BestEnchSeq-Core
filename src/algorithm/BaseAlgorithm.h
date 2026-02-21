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

    struct Config {
        bool ignore_penalty_cost;
        bool ignore_repair_cost;
    };

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

    virtual void _init(const Config &config) = 0;
    virtual void _run(const Input &input)    = 0;
    virtual bool _stop()                     = 0;

  public:
    BaseAlgorithm()          = default;
    virtual ~BaseAlgorithm() = default;

    void init(const Config &config);
    void run(const Input &input);
    void stop();
    State get_state() const noexcept;
    Output get_output() const;

    virtual std::pair<ItemStack, int32_t>
    forge_item(const ItemStack &item_a, const ItemStack &item_b, bool updated = false) const;
};

int32_t calc_exp(int32_t level);
