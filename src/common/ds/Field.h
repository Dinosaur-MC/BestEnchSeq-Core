#pragma once
#include <type_traits>
#include <utility>
#include <vector>

namespace ds {

// ── 成员指针 traits ──────────────────────────────────────────────────
template<typename Ptr> struct member_traits;
template<typename T, typename V> struct member_traits<V T::*> {
    using class_type = T;
    using value_type = V;
};

// ── 默认发射谓词：总是发射 ──────────────────────────────────────────
template<typename T> struct AlwaysEmit {
    bool operator()(const T&) const noexcept { return true; }
};

// ── 字段描述符 ───────────────────────────────────────────────────────
// name = JSON 键名 / CSV 表头；member = 运行时成员指针（类型安全）；
// codec = 鸭子类型（to_json/from_json/to_csv/from_csv）；
// emit = bool(const class_type&) 发射谓词（默认恒真，如条件字段）；
// aliases = 解析时备用 JSON key 路径（主键缺省时依次尝试，如旧键名/嵌套形态）；
// required = 解析时主键与别名都缺省则报"missing required"。
template<typename T, typename Ptr, typename Codec, typename Emit = AlwaysEmit<T>>
struct Field {
    const char* name;
    Ptr member;
    Codec codec;
    Emit emit;
    std::vector<const char*> aliases;
    bool required = false;

    using value_type = typename member_traits<Ptr>::value_type;

    const value_type& get(const T& o) const { return o.*member; }
    void set(T& o, const value_type& v) const { o.*member = v; }
    bool should_emit(const T& o) const { return emit(o); }
};

// ── 工厂：普通字段（可选，别名可选）────────────────────────────────
template<typename Ptr, typename Codec, typename Emit>
    requires std::is_invocable_v<Emit, const typename member_traits<Ptr>::class_type&>
auto field(const char* name, Ptr member, Codec codec, Emit emit) {
    using traits = member_traits<Ptr>;
    return Field<typename traits::class_type, Ptr, Codec, Emit>{
        name, member, std::move(codec), std::move(emit), {}, false};
}

template<typename Ptr, typename Codec, typename... Alias>
auto field(const char* name, Ptr member, Codec codec, Alias... aliases) {
    using traits = member_traits<Ptr>;
    return Field<typename traits::class_type, Ptr, Codec>{
        name, member, std::move(codec), AlwaysEmit<typename traits::class_type>{},
        {aliases...}, false};
}

// ── 工厂：必填字段 ──────────────────────────────────────────────────
template<typename Ptr, typename Codec, typename... Alias>
auto required_field(const char* name, Ptr member, Codec codec, Alias... aliases) {
    using traits = member_traits<Ptr>;
    return Field<typename traits::class_type, Ptr, Codec>{
        name, member, std::move(codec), AlwaysEmit<typename traits::class_type>{},
        {aliases...}, true};
}

} // namespace ds
