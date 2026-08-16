#include "CommonTypes.h"
#include "utils/StringUtils.hpp"
#include <stdexcept>

namespace {
// namespace: [a-z0-9_.-], no '/', no uppercase, non-empty, not "." or ".."
inline bool validate_ns(const std::string_view &s) {
    static constexpr std::string_view valid = "0123456789abcdefghijklmnopqrstuvwxyz_.-";

    if (s.empty() || s == "." || s == "..")
        return false;
    if (s.find_first_not_of(valid) != std::string_view::npos)
        return false;
    return true;
}

// path: [a-z0-9/._-], no uppercase, non-empty, no "/"-delimited segment equal to "." or ".."
inline bool validate_path(const std::string_view &s) {
    static constexpr std::string_view valid = "0123456789abcdefghijklmnopqrstuvwxyz/._-";

    if (s.empty())
        return false;
    if (s.find_first_not_of(valid) != std::string_view::npos)
        return false;
    // no "." or ".." segment (split on '/'; a segment equal to '.' or '..' is rejected)
    size_t start = 0;
    for (;;) {
        auto slash = s.find('/', start);
        auto seg   = s.substr(start, slash == std::string_view::npos ? s.size() - start : slash - start);
        if (seg == "." || seg == "..")
            return false;
        if (slash == std::string_view::npos)
            break;
        start = slash + 1;
    }
    return true;
}

} // namespace

NSID::NSID(const std::string_view &ns, const std::string_view &id) : _ns(ns), _id(id) {
    if (_ns.empty()) {
        _ns = "minecraft";
    } else if (_ns.starts_with("#")) {
        _ns     = _ns.substr(1);
        _is_tag = true;
    }
    if (_id.starts_with("#")) {
        _id     = _id.substr(1);
        _is_tag = true;
    }
    if (!validate_ns(_ns) || !validate_path(_id)) {
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

std::string NSID::str() const {
    if (_ns.empty() || _id.empty())
        return "";
    return _is_tag ? "#" + _ns + ":" + _id : _ns + ":" + _id;
}

NSID &NSID::operator=(const char *strid) {
    *this = NSID(strid);
    return *this;
}
NSID &NSID::operator=(const std::string_view &strid) {
    *this = NSID(strid);
    return *this;
}
