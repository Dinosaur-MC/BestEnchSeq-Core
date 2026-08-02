#pragma once
#include "common/io/json.h"
#include "ds/Error.h"

#include <optional>
#include <string>
#include <string_view>

namespace ds {

/// text_codec：标量 ↔ 字符串（JSON 用 string，CSV 用 text）。
/// Conv 约定：using value_type = T; static std::string to_string(const T&);
///            static std::optional<T> from_string(std::string_view); // nullopt = 校验失败
template<typename Conv>
struct text_codec {
    using V = typename Conv::value_type;
    void to_json(const V& v, Json& out) const { out = Json(Conv::to_string(v)); }
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (j.type() != JsonType::String) { e.add(path, "expected string"); return false; }
        auto r = Conv::from_string(j.as<std::string>());
        if (!r) { e.add(path, "invalid value"); return false; }
        v = std::move(*r); return true;
    }
    void to_csv(const V& v, std::string& out) const { out = Conv::to_string(v); }
    bool from_csv(const std::string_view& s, V& v, ErrorList& e, const std::string& path) const {
        auto r = Conv::from_string(s);
        if (!r) { e.add(path, "invalid value"); return false; }
        v = std::move(*r); return true;
    }
};

/// json_codec：任意 ↔ Json（富表示；CSV 无自然表示，报错）。
/// Conv 约定：using value_type = T; static Json to_json(const T&);
///            static bool from_json(const Json&, T&); // false = 校验失败
template<typename Conv>
struct json_codec {
    using V = typename Conv::value_type;
    void to_json(const V& v, Json& out) const { out = Conv::to_json(v); }
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (!Conv::from_json(j, v)) { e.add(path, "invalid value"); return false; }
        return true;
    }
    void to_csv(const V&, std::string&) const {}
    bool from_csv(const std::string_view&, V&, ErrorList& e, const std::string& path) const {
        e.add(path, "field has no CSV representation"); return false;
    }
};

/// enum_codec：枚举 ↔ 字符串（内部用 text_codec）。
template<typename Conv>
using enum_codec = text_codec<Conv>;

} // namespace ds
