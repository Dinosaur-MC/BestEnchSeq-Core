#pragma once
#include "common/io/json.h"
#include "ds/Error.h"

#include <string>
#include <tuple>
#include <utility>

namespace ds::json {

/// 取 JSON 键值：优先主键，缺省时依次尝试别名路径（Json::at 支持点路径）。
/// aliases 为任意可范围遍历的 const char* 容器（Field 用 std::array 定长存储）。
/// 找到返回 true，out 置该值；全缺省返回 false（out 为 null）。
template<typename Aliases>
inline bool get_key(const Json& obj, const std::string& key,
                    const Aliases& aliases, Json& out) {
    if (obj.has(key)) { out = obj[key]; return true; }
    for (const char* a : aliases) {
        Json v = obj.at(a, Json());
        if (v.type() != JsonType::Empty && v.type() != JsonType::Null) { out = std::move(v); return true; }
    }
    out = Json(); return false;
}

/// 物理 JSON 绑定：S 为逻辑 schema（S::Type + S::fields），Strict=true 时未知键报错。
/// 默认实参 Strict=false 由 Codecs.h 的前置声明提供（object_codec 成员模板需在解析期
/// 看到默认实参完成 `Schema<SubSchema>` 实参数目检查；同作用域不能重复指定默认实参）。
template<typename S, bool Strict>
struct Schema {
    using Type = typename S::Type;

    // ── 序列化 ──
    static Json serialize(const Type& o) {
        Json obj = Json::object();
        std::apply([&](const auto&... f) { (set_field(f, o, obj), ...); }, S::fields);
        return obj;
    }
    template<typename F>
    static void set_field(const F& f, const Type& o, Json& obj) {
        if (!f.should_emit(o)) return;
        Json j;
        f.codec.to_json(f.get(o), j);
        obj.set(f.name, std::move(j));
    }

    // ── 反序列化（收集式）──
    static bool parse(const Json& obj, Type& o, ErrorList& err) {
        return parse(obj, o, err, "");
    }
    /// 带路径前缀重载：嵌套对象（object_codec）把自身路径传入（如 "home"/"items[0]"），
    /// 使内层字段错误带完整路径（"home.street"/"items[0].type"），避免数组元素间歧义。
    static bool parse(const Json& obj, Type& o, ErrorList& err, const std::string& prefix) {
        if (obj.type() != JsonType::Object) { err.add(prefix, "expected object"); return false; }
        bool ok = true;
        std::apply([&](const auto&... f) { (parse_field(f, obj, o, err, ok, prefix), ...); }, S::fields);
        if constexpr (Strict)
            check_unknown_keys(obj, err, prefix);
        // 跨字段校验钩子：S 定义 static validate(Type&, ErrorList&) 时调用（spec §5）。
        if constexpr (requires { S::validate(o, err); })
            S::validate(o, err);
        return ok && err.empty();
    }
    static std::string field_path(const std::string& prefix, const char* name) {
        return prefix.empty() ? std::string(name) : prefix + "." + name;
    }
    template<typename F>
    static void parse_field(const F& f, const Json& obj, Type& o, ErrorList& err, bool& ok,
                            const std::string& prefix) {
        std::string fp = field_path(prefix, f.name);
        Json raw;
        if (get_key(obj, f.name, f.aliases, raw)) {
            typename F::value_type v{};
            if (f.codec.from_json(raw, v, err, fp)) {
                f.set(o, std::move(v));
                if constexpr (F::Presence != nullptr)
                    o.*(F::Presence) = true;
            } else {
                ok = false;
                // 解析失败也清除旗标，避免复用对象残留上一次的 true。
                if constexpr (F::Presence != nullptr)
                    o.*(F::Presence) = false;
            }
        } else if (f.required) {
            err.add(fp, "missing required field");
            ok = false;
            if constexpr (F::Presence != nullptr)
                o.*(F::Presence) = false;
        } else if constexpr (F::Presence != nullptr) {
            o.*(F::Presence) = false;
        }
        // 非必填且缺省 → 保持默认
    }
    static void check_unknown_keys(const Json& obj, ErrorList& err, const std::string& prefix) {
        for (const auto& [key, _] : obj.as_object()) {
            bool known = std::apply([&](const auto&... f) {
                return ((std::string(f.name) == key) || ...);
            }, S::fields);
            if (!known) err.add(prefix.empty() ? key : prefix + "." + key, "unknown field");
        }
    }

    // ── 便利：有错聚合抛出 ──
    static void parse_or_throw(const Json& obj, Type& o) {
        ErrorList err;
        parse(obj, o, err);
        if (!err.empty()) throw ValidationError(std::move(err));
    }
};

} // namespace ds::json
