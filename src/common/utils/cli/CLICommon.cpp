#include "CLICommon.h"
#include <charconv>
#include <cstring>

bool from_string(std::string_view sv, int& out) noexcept {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

bool from_string(std::string_view sv, long& out) noexcept {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

bool from_string(std::string_view sv, float& out) noexcept {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

bool from_string(std::string_view sv, double& out) noexcept {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

bool from_string(std::string_view sv, bool& out) noexcept {
    if (sv == "true" || sv == "1")  { out = true;  return true; }
    if (sv == "false" || sv == "0") { out = false; return true; }
    return false;
}

bool from_string(std::string_view sv, std::string& out) noexcept {
    out = sv;
    return true;
}
