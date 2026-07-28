#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace string_utils {

inline std::string to_lower(const std::string_view &str) {
    std::string result = std::string(str);
    for (char &c : result)
        c = std::tolower(c);
    return result;
}
inline std::string to_upper(const std::string_view &str) {
    std::string result = std::string(str);
    for (char &c : result)
        c = std::toupper(c);
    return result;
}
inline std::string trim(const std::string_view &str) {
    auto begin = str.find_first_not_of(" \t\n\r");
    auto end   = str.find_last_not_of(" \t\n\r") + 1;
    if (begin == std::string_view::npos || end == std::string_view::npos)
        return std::string();
    return std::string(str.substr(begin, end - begin));
}
inline std::string trim_left(const std::string_view &str) {
    std::string result = std::string(str);
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    return result;
}
inline std::string trim_right(const std::string_view &str) {
    std::string result = std::string(str);
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    return result;
}
inline std::vector<std::string>
split(const std::string_view &str, std::string_view delimiters, bool keep_empty = true) {
    std::vector<std::string> result;
    std::string_view token;
    size_t pos      = 0;
    size_t last_pos = 0;
    while ((pos = str.find_first_of(delimiters, last_pos)) != std::string_view::npos) {
        token    = str.substr(last_pos, pos - last_pos);
        last_pos = pos + delimiters.size();
        if (!token.empty() || keep_empty)
            result.emplace_back(token);
    }
    token = str.substr(last_pos);
    if (!token.empty() || keep_empty)
        result.emplace_back(token);
    return result;
}
template <typename T>
    requires std::is_convertible_v<T, std::string_view>
inline std::string join(const std::vector<T> &tokens, std::string_view delimiters) {
    std::string result;
    for (auto &token : tokens) {
        if (!result.empty())
            result += delimiters;
        result += std::string(token);
    }
    return result;
}
template <
    typename Iter,
    std::enable_if_t<std::is_convertible_v<decltype(*std::declval<Iter &>()), std::string_view>, int> = 0>
inline std::string join(const Iter begin, const Iter end, std::string_view delimiters) {
    std::string result;
    for (auto token = begin; token != end; ++token) {
        if (!result.empty())
            result += delimiters;
        result += std::string(*token);
    }
    return result;
}

// Single-character delimiter split (skips empty tokens, matches old ParserUtils::split_string semantics)
inline std::vector<std::string> split(const std::string_view &str, char delimiter) {
    std::vector<std::string> tokens;
    if (str.empty())
        return tokens;
    size_t start = 0;
    while (true) {
        size_t end = str.find(delimiter, start);
        if (end == std::string::npos) {
            if (start < str.size())
                tokens.emplace_back(str.substr(start));
            break;
        }
        if (end > start)
            tokens.emplace_back(str.substr(start, end - start));
        start = end + 1;
    }
    return tokens;
}

}; // namespace string_utils
