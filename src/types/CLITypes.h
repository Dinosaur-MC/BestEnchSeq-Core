#pragma once
#include <optional>
#include <string>
#include <vector>

// ─── Application-level CLI configuration ──────────────────────────────────
struct CLIConfig {
    std::string algorithm = "greedy";
    std::string mode = "direct";
    std::string target;
    std::string wanted;
    std::optional<std::string> input;
    std::optional<std::string> data_pack;
    std::string platform = "auto";
    std::optional<std::string> output;
    std::string format = "text";
    int solutions = 1;
    int memory_mb = 0;
    bool verbose = false;
    bool ignore_cost_cap = false;
    bool help = false;
};

struct EnchantmentSpec {
    std::string ns = "minecraft";
    std::string id;
    int level = 1;
};

struct TargetSpec {
    std::string item_id;
    std::vector<EnchantmentSpec> inline_enchants;
};
