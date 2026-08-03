#pragma once
#include "common/io/json.h"
#include "ds/Error.h"

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// 前置声明 ds::json::Schema：object_codec 的成员模板在实例化点（用户 schema 文件
// 已包含完整 ds/ds.h → JsonBinder.h）才解析 Schema 的完整定义；此处仅需模板名可见，
// 以把 `Schema<SubSchema>` 解析为类模板（非依赖限定名在定义期查找）。
// 必须在此携带 Strict=false 默认实参：成员模板体在解析期就对 `Schema<SubSchema>`
// 做实参数目检查（2 参模板给 1 参），若默认实参不可见即报 "too few template arguments"。
// 故默认实参的唯一声明位置迁移到此处，JsonBinder.h 的定义不再重复指定（同作用域
// 重复指定默认实参为 ill-formed）。Codecs.h 恒先于 JsonBinder.h 被包含（ds.h 顺序，
// 且 JsonBinder.h 自身 include Codecs.h），故默认实参全局可见。
namespace ds::json {
template<typename, bool = false>
struct Schema;
}

namespace ds {

// ── string ───────────────────────────────────────────────────────────
struct string_codec {
    template<class V> void to_json(const V& v, Json& out) const { out = Json(std::string(v)); }
    template<class V>
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (j.type() != JsonType::String) { e.add(path, "expected string"); return false; }
        v = j.as<std::string>(); return true;
    }
    template<class V> void to_csv(const V& v, std::string& out) const { out = std::string(v); }
    template<class V>
    bool from_csv(const std::string_view& s, V& v, ErrorList&, const std::string&) const {
        v = std::string(s); return true;
    }
};

// ── int_（聚合：min/max，默认全范围）───────────────────────────────
struct int_codec {
    int64_t min = INT64_MIN;
    int64_t max = INT64_MAX;

    template<class V> requires std::integral<V>
    void to_json(const V& v, Json& out) const { out = Json(static_cast<int64_t>(v)); }
    template<class V> requires std::integral<V>
    bool from_json(const Json& j, V& v_out, ErrorList& e, const std::string& path) const {
        auto val = j.get_value();
        const auto* num = std::get_if<Json::Number>(&val);
        if (!num) { e.add(path, "expected number"); return false; }
        if (const auto* i = std::get_if<int64_t>(num)) {
            if (*i < min || *i > max) { e.add(path, "out of range"); return false; }
            v_out = static_cast<V>(*i); return true;
        }
        if (const auto* d = std::get_if<double>(num)) {
            if (*d != static_cast<int64_t>(*d)) { e.add(path, "expected integer"); return false; }
            int64_t n = static_cast<int64_t>(*d);
            if (n < min || n > max) { e.add(path, "out of range"); return false; }
            v_out = static_cast<V>(n); return true;
        }
        e.add(path, "expected number"); return false;
    }
    template<class V> requires std::integral<V>
    void to_csv(const V& v, std::string& out) const { out = std::to_string(v); }
    template<class V> requires std::integral<V>
    bool from_csv(const std::string_view& s, V& v, ErrorList& e, const std::string& path) const {
        int64_t n = 0;
        const char* end = s.data() + s.size();
        auto [ptr, ec] = std::from_chars(s.data(), end, n);
        if (ec != std::errc() || ptr != end) { e.add(path, "invalid integer"); return false; }
        if (n < min || n > max) { e.add(path, "out of range"); return false; }
        v = static_cast<V>(n); return true;
    }
};

// ── float_ ───────────────────────────────────────────────────────────
struct float_codec {
    template<class V> requires std::floating_point<V>
    void to_json(const V& v, Json& out) const { out = Json(static_cast<double>(v)); }
    template<class V> requires std::floating_point<V>
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (j.type() != JsonType::Number) { e.add(path, "expected number"); return false; }
        v = static_cast<V>(j.as<double>()); return true;
    }
    template<class V> requires std::floating_point<V>
    void to_csv(const V& v, std::string& out) const {
        char buf[32];
        auto res = std::to_chars(buf, buf + sizeof(buf), static_cast<double>(v));
        out.assign(buf, res.ptr);
    }
    template<class V> requires std::floating_point<V>
    bool from_csv(const std::string_view& s, V& v, ErrorList& e, const std::string& path) const {
        double dval = 0;
        const char* end = s.data() + s.size();
        auto [ptr, ec] = std::from_chars(s.data(), end, dval, std::chars_format::general);
        if (ec != std::errc() || ptr != end) { e.add(path, "invalid number"); return false; }
        v = static_cast<V>(dval); return true;
    }
};

// ── bool_ ────────────────────────────────────────────────────────────
struct bool_codec {
    template<class V> void to_json(const V& v, Json& out) const { out = Json(bool(v)); }
    template<class V>
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (j.type() != JsonType::Bool) { e.add(path, "expected bool"); return false; }
        v = j.as<bool>(); return true;
    }
    template<class V> void to_csv(const V& v, std::string& out) const { out = v ? "true" : "false"; }
    template<class V>
    bool from_csv(const std::string_view& s, V& v, ErrorList& e, const std::string& path) const {
        if (s == "true" || s == "1") { v = true; return true; }
        if (s == "false" || s == "0") { v = false; return true; }
        e.add(path, "invalid bool"); return false;
    }
};

// ── object_codec（嵌套对象）─────────────────────────────────────────
/// 嵌套对象 codec：SubSchema 为逻辑 schema（S::Type + S::fields），
/// 复用 ds::json::Schema 的序列化/反序列化，用于表示 JSON 中的嵌套对象字段
/// （如 inventory task 的 target 子对象）。与 vector_codec 组合得到对象数组。
/// 嵌套对象无 CSV 自然表示：to_csv 写占位符，from_csv 记错拒绝。
template<typename SubSchema>
struct object_codec {
    template<class V>  // V = SubSchema::Type
    void to_json(const V& obj, Json& out) const {
        out = ds::json::Schema<SubSchema>::serialize(obj);
    }
    template<class V>
    bool from_json(const Json& j, V& obj, ErrorList& err, const std::string& path) const {
        if (j.type() != JsonType::Object) { err.add(path, "expected object"); return false; }
        return ds::json::Schema<SubSchema>::parse(j, obj, err);
    }
    template<class V>
    void to_csv(const V&, std::string& out) const { out += "<nested-object>"; }
    template<class V>
    bool from_csv(const std::string_view&, V&, ErrorList& err, const std::string& path) const {
        err.add(path, "nested object not supported in CSV");
        return false;
    }
};

// ── vector_codec ─────────────────────────────────────────────────────
/// NOTE: elements must not contain the CSV list separator ';' in their
/// serialized form (guaranteed for NSID/identifier sets; escaping is future work).
template<typename InnerCodec>
struct vector_codec {
    InnerCodec inner;
    template<class V>  // V = std::vector<Elem>
    void to_json(const V& vec, Json& out) const {
        Json arr = Json::array();
        for (const auto& e : vec) {
            Json item;
            inner.to_json(e, item);
            arr.push_back(std::move(item));
        }
        out = std::move(arr);
    }
    template<class V>
    bool from_json(const Json& j, V& vec, ErrorList& err, const std::string& path) const {
        if (j.type() != JsonType::Array) {
            vec.clear();
            err.add(path, "expected array");
            return false;
        }
        vec.clear();
        auto arr = j.as<Json::Array>();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            typename V::value_type elem{};
            std::string p = path + "[" + std::to_string(i) + "]";
            if (inner.from_json(arr[i], elem, err, p))
                vec.push_back(std::move(elem));
        }
        return true;
    }
    template<class V>
    void to_csv(const V& vec, std::string& out) const {
        bool first = true;
        for (const auto& e : vec) {
            if (!first) out += ';';
            first = false;
            std::string cell;
            inner.to_csv(e, cell);
            out += cell;
        }
    }
    template<class V>
    bool from_csv(const std::string_view& s, V& vec, ErrorList& err, const std::string& path) const {
        vec.clear();
        if (s.empty()) return true;
        std::size_t start = 0;
        std::size_t elem_index = 0;
        while (start <= s.size()) {
            std::size_t end = s.find(';', start);
            if (end == std::string_view::npos) end = s.size();
            typename V::value_type elem{};
            std::string p = path + "[" + std::to_string(elem_index) + "]";
            if (inner.from_csv(s.substr(start, end - start), elem, err, p))
                vec.push_back(std::move(elem));
            if (end == s.size()) break;
            start = end + 1;
            ++elem_index;
        }
        return true;
    }
};

// ── set_codec（序列化排序保证确定性）──────────────────────────────
/// NOTE: elements must not contain the CSV list separator ';' in their
/// serialized form (guaranteed for NSID/identifier sets; escaping is future work).
template<typename InnerCodec>
struct set_codec {
    InnerCodec inner;
    template<class V>  // V = std::set/std::unordered_set<Elem>
    void to_json(const V& set, Json& out) const {
        // 以 CSV 文本为排序键保证确定性，元素按 inner.to_json 发射保证任意类型往返一致。
        std::vector<std::pair<std::string, Json>> cells;   // {sortKey, json}
        for (const auto& e : set) {
            std::string key;
            inner.to_csv(e, key);
            Json item;
            inner.to_json(e, item);
            cells.push_back({std::move(key), std::move(item)});
        }
        std::sort(cells.begin(), cells.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        Json arr = Json::array();
        for (auto& [key, item] : cells)
            arr.push_back(std::move(item));
        out = std::move(arr);
    }
    template<class V>
    bool from_json(const Json& j, V& set, ErrorList& err, const std::string& path) const {
        if (j.type() != JsonType::Array) { err.add(path, "expected array"); return false; }
        set.clear();
        auto arr = j.as<Json::Array>();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            typename V::value_type elem{};
            std::string p = path + "[" + std::to_string(i) + "]";
            if (inner.from_json(arr[i], elem, err, p))
                set.insert(std::move(elem));
        }
        return true;
    }
    template<class V>
    void to_csv(const V& set, std::string& out) const {
        std::vector<std::string> cells;
        for (const auto& e : set) {
            std::string cell;
            inner.to_csv(e, cell);
            cells.push_back(std::move(cell));
        }
        std::sort(cells.begin(), cells.end());
        bool first = true;
        for (const auto& c : cells) {
            if (!first) out += ';';
            first = false;
            out += c;
        }
    }
    template<class V>
    bool from_csv(const std::string_view& s, V& set, ErrorList& err, const std::string& path) const {
        set.clear();
        if (s.empty()) return true;
        std::size_t start = 0;
        std::size_t elem_index = 0;
        while (start <= s.size()) {
            std::size_t end = s.find(';', start);
            if (end == std::string_view::npos) end = s.size();
            typename V::value_type elem{};
            std::string p = path + "[" + std::to_string(elem_index) + "]";
            if (inner.from_csv(s.substr(start, end - start), elem, err, p))
                set.insert(std::move(elem));
            if (end == s.size()) break;
            start = end + 1;
            ++elem_index;
        }
        return true;
    }
};

// ── optional_codec ───────────────────────────────────────────────────
template<typename InnerCodec>
struct optional_codec {
    InnerCodec inner;
    template<class V>  // V = std::optional<Elem>
    void to_json(const V& opt, Json& out) const {
        if (opt.has_value()) inner.to_json(*opt, out);
        else out = Json::null();
    }
    template<class V>
    bool from_json(const Json& j, V& opt, ErrorList& err, const std::string& path) const {
        if (j.type() == JsonType::Null) { opt.reset(); return true; }
        typename V::value_type elem{};
        if (!inner.from_json(j, elem, err, path)) return false;
        opt = std::move(elem);
        return true;
    }
    template<class V>
    void to_csv(const V& opt, std::string& out) const {
        if (opt.has_value()) inner.to_csv(*opt, out);
        // else 空串
    }
    template<class V>
    bool from_csv(const std::string_view& s, V& opt, ErrorList& err, const std::string& path) const {
        if (s.empty()) { opt.reset(); return true; }
        typename V::value_type elem{};
        if (!inner.from_csv(s, elem, err, path)) return false;
        opt = std::move(elem);
        return true;
    }
};

} // namespace ds
