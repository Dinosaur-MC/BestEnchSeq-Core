#include "EnchParser.h"
#include "ItemParser.h"
#include <stdexcept>

TargetSpec ItemParser::parse(const std::string &input) {
    TargetSpec result;
    auto bracket_pos = input.find('[');
    if (bracket_pos != std::string::npos) {
        auto close_pos = input.find(']', bracket_pos);
        if (close_pos == std::string::npos)
            throw std::runtime_error("Malformed target spec: missing ']' in '" + input + "'");

        result.item_id         = input.substr(0, bracket_pos);
        std::string inline_str = input.substr(bracket_pos + 1, close_pos - bracket_pos - 1);
        result.inline_enchants = EnchParser::parse(inline_str);

        // Reject trailing content after closing bracket
        auto trailing = input.find_first_not_of(" \t", close_pos + 1);
        if (trailing != std::string::npos)
            throw std::runtime_error(
                "Malformed target spec: unexpected content after ']' in '" + input + "'"
            );
    } else {
        result.item_id = input;
    }
    if (result.item_id.empty())
        throw std::runtime_error("Empty item id in target spec");
    return result;
}
