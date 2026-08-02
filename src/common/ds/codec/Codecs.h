#pragma once
#include "common/io/json.h"
#include "ds/Error.h"

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

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

// ── vector_codec ─────────────────────────────────────────────────────
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
        if (j.type() != JsonType::Array) { err.add(path, "expected array"); return false; }
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
        while (start <= s.size()) {
            std::size_t end = s.find(';', start);
            if (end == std::string_view::npos) end = s.size();
            typename V::value_type elem{};
            if (inner.from_csv(s.substr(start, end - start), elem, err, path))
                vec.push_back(std::move(elem));
            if (end == s.size()) break;
            start = end + 1;
        }
        return true;
    }
};

// ── set_codec（序列化排序保证确定性）──────────────────────────────
template<typename InnerCodec>
struct set_codec {
    InnerCodec inner;
    template<class V>  // V = std::set/std::unordered_set<Elem>
    void to_json(const V& set, Json& out) const {
        std::vector<std::string> cells;
        for (const auto& e : set) {
            std::string cell;
            inner.to_csv(e, cell);          // 用 CSV 文本作排序键
            cells.push_back(std::move(cell));
        }
        std::sort(cells.begin(), cells.end());
        Json arr = Json::array();
        for (const auto& c : cells)
            arr.push_back(Json(c));          // 元素为标量文本（NSID/string 等）
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
        while (start <= s.size()) {
            std::size_t end = s.find(';', start);
            if (end == std::string_view::npos) end = s.size();
            typename V::value_type elem{};
            if (inner.from_csv(s.substr(start, end - start), elem, err, path))
                set.insert(std::move(elem));
            if (end == s.size()) break;
            start = end + 1;
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
