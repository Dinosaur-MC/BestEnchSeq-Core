#include "EnchParser.h"
#include "ItemParser.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include <stdexcept>

Item ItemParser::parse(const std::string &input,
                       const EnchantmentRegistry &ench_reg,
                       const EquipmentRegistry &eq_reg)
{
    std::string item_id;
    EnchSet ench_set;

    auto bracket_pos = input.find('[');
    if (bracket_pos != std::string::npos) {
        auto close_pos = input.find(']', bracket_pos);
        if (close_pos == std::string::npos)
            throw std::runtime_error("Malformed target spec: missing ']' in '" + input + "'");

        item_id = input.substr(0, bracket_pos);
        std::string inline_str = input.substr(bracket_pos + 1, close_pos - bracket_pos - 1);
        if (!inline_str.empty())
            ench_set = EnchParser::parse(inline_str, ench_reg);

        // Reject trailing content after closing bracket
        auto trailing = input.find_first_not_of(" \t", close_pos + 1);
        if (trailing != std::string::npos)
            throw std::runtime_error(
                "Malformed target spec: unexpected content after ']' in '" + input + "'"
            );
    } else {
        item_id = input;
    }

    if (item_id.empty())
        throw std::runtime_error("Empty item id in target spec");

    // Look up equipment in registry
    auto eq_it = eq_reg.find(NSID(item_id));
    if (eq_it == eq_reg.end())
        throw std::runtime_error("Unknown equipment: '" + item_id + "'");

    return Item(eq_it->id, ench_set, 0);
}
