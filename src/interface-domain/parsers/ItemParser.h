#pragma once
#include "../cli/cli.h"
#include <string>

struct ItemParser {
    /// Parse target spec: <item_id>[<ench>=<level>,...]
    /// Bare item_id also accepted (no inline enchants).
    /// Throws std::runtime_error on unmatched brackets or trailing content after ']'.
    static TargetSpec parse(const std::string &input);
};
