#pragma once
#include "common/CommonTypes.h"
#include "domain/business/components/Serializer.h"

#include <optional>
#include <string>
#include <string_view>

namespace business::schema {

/// NSID ↔ 字符串（text_codec 契约：nullopt = 校验失败）。
struct NSIDConverter {
    using value_type = NSID;
    static std::string to_string(const NSID& id) { return id.str(); }
    static std::optional<NSID> from_string(std::string_view s) {
        try { return NSID(s); } catch (...) { return std::nullopt; }
    }
};

/// MCE 平台枚举 ↔ 字符串。与 Serializer::mce_to_string/string_to_mce 完全一致
/// （"none"/"java"/"bedrock"/"all"，未知 → MCE::None）。单一事实源。
struct PlatformConv {
    using value_type = MCE;
    static std::string to_string(MCE p) {
        return std::string(Serializer::mce_to_string(p));
    }
    static std::optional<MCE> from_string(std::string_view s) {
        return Serializer::string_to_mce(s);   // 永不 nullopt（未知→None，兼容旧行为）
    }
};

} // namespace business::schema
