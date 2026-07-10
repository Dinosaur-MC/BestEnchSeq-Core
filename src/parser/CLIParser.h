#pragma once
#include <optional>
#include <string>
#include <vector>

struct CLIConfig {
    std::string algorithm = "greedy";     // "greedy" | "dfs"
    std::string mode = "direct";          // "direct" | "inventory"
    std::string target;                   // target item spec (may include inline enchants)
    std::string wanted;                   // wanted enchantment list string
    std::optional<std::string> input;     // input file path (inventory mode)
    std::optional<std::string> data_pack; // custom data pack directory
    std::string platform = "auto";        // "java" | "bedrock" | "auto"
    std::optional<std::string> output;    // output file path (default stdout)
    std::string format = "text";          // "text" | "compact" | "json"
    int solutions = 1;                    // max solutions (0 = unlimited)
    int memory_mb = 0;                    // 0 = auto (AStar only)
    bool ignore_cost_cap = false; // bypass 39-level survival cap
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

class CLIParser {
  public:
    CLIConfig parse(int argc, char *argv[]);

    static EnchantmentSpec parse_enchantment(const std::string &spec);
    static std::vector<EnchantmentSpec> parse_enchantment_list(const std::string &list);
    static TargetSpec parse_target(const std::string &target);
    static std::string get_help_text(const std::string &program_name = "besq");
};
