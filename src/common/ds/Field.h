#pragma once
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

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

/// Marker: bind a bool member to this field's PRESENCE — when the field is
/// found (key present + value parses) on parse, the binder sets it true.
template<auto FlagPtr>
struct presence_flag {
    static constexpr auto value = FlagPtr;
};

// ── 字段描述符 ───────────────────────────────────────────────────────
// name = JSON 键名 / CSV 表头；member = 运行时成员指针（类型安全）；
// codec = 鸭子类型（to_json/from_json/to_csv/from_csv）；
// emit = bool(const class_type&) 发射谓词（默认恒真，如条件字段）；
// aliases = 解析时备用 JSON key 路径（主键缺省时依次尝试，如旧键名/嵌套形态），
//           以 std::array 定长存储（AliasN 为编译期别名数），使 Field 保持 literal 类型，
//           从而 schema 的 fields tuple 可 static constexpr 求值；
// required = 解析时主键与别名都缺省则报"missing required"；
// PresenceFlag = 指向 bool 成员的指针（presence_flag 指定）：解析成功 → true，
//               缺省 → false。字段 codec 只能触达本字段值，无法重建兄弟成员，
//               故由 binder 在解析时直接写该旗标。
template<typename T, typename Ptr, typename Codec, typename Emit = AlwaysEmit<T>,
         std::size_t AliasN = 0, bool T::* PresenceFlag = nullptr>
struct Field {
    const char* name;
    Ptr member;
    Codec codec;
    Emit emit;
    std::array<const char*, AliasN> aliases;
    bool required = false;

    using value_type = typename member_traits<Ptr>::value_type;

    // 暴露 presence 成员指针给 binder 读取（F::Presence）。
    // 非类型模板参数不能经 `::` 直接取名，故以 static constexpr 成员暴露。
    static constexpr bool T::* Presence = PresenceFlag;

    const value_type& get(const T& o) const { return o.*member; }
    void set(T& o, const value_type& v) const { o.*member = v; }
    bool should_emit(const T& o) const { return emit(o); }
};

// ── 工厂：普通字段（可选，别名可选）────────────────────────────────
template<typename Ptr, typename Codec, typename Emit>
    requires std::is_invocable_v<Emit, const typename member_traits<Ptr>::class_type&>
constexpr auto field(const char* name, Ptr member, Codec codec, Emit emit) {
    using traits = member_traits<Ptr>;
    using T = typename traits::class_type;
    return Field<T, Ptr, Codec, Emit, 0>{
        name, member, std::move(codec), std::move(emit), {}, false};
}

template<typename Ptr, typename Codec, typename... Alias>
constexpr auto field(const char* name, Ptr member, Codec codec, Alias... aliases) {
    using traits = member_traits<Ptr>;
    using T = typename traits::class_type;
    return Field<T, Ptr, Codec, AlwaysEmit<T>, sizeof...(Alias)>{
        name, member, std::move(codec), AlwaysEmit<T>{}, {aliases...}, false};
}

// ── 工厂：必填字段 ──────────────────────────────────────────────────
template<typename Ptr, typename Codec, typename... Alias>
constexpr auto required_field(const char* name, Ptr member, Codec codec, Alias... aliases) {
    using traits = member_traits<Ptr>;
    using T = typename traits::class_type;
    return Field<T, Ptr, Codec, AlwaysEmit<T>, sizeof...(Alias)>{
        name, member, std::move(codec), AlwaysEmit<T>{}, {aliases...}, true};
}

// ── 工厂：presence flag（仅旗标，可选字段）─────────────────────────
template<auto FlagPtr, typename Ptr, typename Codec>
constexpr auto field(const char* name, Ptr member, Codec codec, presence_flag<FlagPtr>) {
    using traits = member_traits<Ptr>;
    using T = typename traits::class_type;
    return Field<T, Ptr, Codec, AlwaysEmit<T>, 0, FlagPtr>{
        name, member, std::move(codec), AlwaysEmit<T>{}, {}, false};
}

// ── 工厂：presence flag + 发射谓词 ─────────────────────────────────
template<typename Ptr, typename Codec, typename Emit, auto FlagPtr>
    requires std::is_invocable_v<Emit, const typename member_traits<Ptr>::class_type&>
constexpr auto field(const char* name, Ptr member, Codec codec, Emit emit, presence_flag<FlagPtr>) {
    using traits = member_traits<Ptr>;
    using T = typename traits::class_type;
    return Field<T, Ptr, Codec, Emit, 0, FlagPtr>{
        name, member, std::move(codec), std::move(emit), {}, false};
}

} // namespace ds
