#include "CommonTypes.h"
#include "utils/StringUtils.hpp"
#include <stdexcept>

namespace {
inline bool validate_id(const std::string_view &id) {
    static constexpr std::string_view valid_chars =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";

    if (id.empty())
        return false;
    if (id.find_first_not_of(valid_chars) != std::string::npos)
        return false;
    if (std::isdigit(id[0]))
        return false;
    return true;
}

} // namespace

NSID::NSID(const std::string_view &ns, const std::string_view &id) : _ns(ns), _id(id) {
    if (_ns.empty()) {
        _ns     = "minecraft";
        _is_tag = false;
    } else if (_ns.starts_with("#")) {
        _ns     = _ns.substr(1);
        _is_tag = true;
    }
    if (!validate_id(_ns) || !validate_id(_id)) {
        throw std::runtime_error("The NSID '" + str() + "' is invalid");
    }
}

NSID::NSID(const std::string_view &strid) {
    auto tokens = string_utils::split(strid, ":");
    if (tokens.size() < 1 || tokens.size() > 2)
        throw std::runtime_error("The NSID '" + std::string(strid) + "' is invalid");
    if (tokens.size() == 2)
        *this = NSID(tokens[0], tokens[1]);
    else
        *this = NSID("", tokens[0]);
}

std::string NSID::str() const { return _is_tag ? "#" + _ns + ":" + _id : _ns + ":" + _id; }

NSID &NSID::operator=(const std::string_view &strid) {
    *this = NSID(strid);
    return *this;
}
