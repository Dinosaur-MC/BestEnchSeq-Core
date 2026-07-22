#include "CommonTypes.h"
#include "utils/StringUtils.hpp"

NSID::NSID(const std::string_view &ns, const std::string_view &id) : _ns(ns), _id(id) {
    if (_id.empty())
        throw std::runtime_error("NSID id is empty");
    if (_ns.empty())
        _ns = "minecraft";
}
NSID::NSID(const std::string_view &nsid) {
    auto tokens = string_utils::split(nsid, ":");
    if (tokens.size() < 1 || tokens.size() > 2)
        throw std::runtime_error("NSID format is invalid");
    if (tokens.size() == 2)
        NSID(tokens[0], tokens[1]);
    else
        NSID("", tokens[0]);
}
