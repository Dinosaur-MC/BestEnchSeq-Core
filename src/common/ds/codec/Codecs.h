#pragma once
#include "common/io/json.h"
#include "ds/Error.h"

#include <string>
#include <string_view>
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
    template<class V> void to_json(const V& v, Json& out) const { out = Json(static_cast<int64_t>(v)); }
    template<class V>
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (j.type() != JsonType::Number) { e.add(path, "expected number"); return false; }
        int64_t n = j.as<int64_t>();
        if (n < min || n > max) { e.add(path, "out of range"); return false; }
        v = static_cast<V>(n); return true;
    }
    template<class V> void to_csv(const V& v, std::string& out) const { out = std::to_string(v); }
    template<class V>
    bool from_csv(const std::string_view& s, V& v, ErrorList& e, const std::string& path) const {
        try { v = static_cast<V>(std::stoll(std::string(s))); return true; }
        catch (...) { e.add(path, "invalid integer"); return false; }
    }
};

// ── float_ ───────────────────────────────────────────────────────────
struct float_codec {
    template<class V> void to_json(const V& v, Json& out) const { out = Json(static_cast<double>(v)); }
    template<class V>
    bool from_json(const Json& j, V& v, ErrorList& e, const std::string& path) const {
        if (j.type() != JsonType::Number) { e.add(path, "expected number"); return false; }
        v = static_cast<V>(j.as<double>()); return true;
    }
    template<class V> void to_csv(const V& v, std::string& out) const { out = std::to_string(v); }
    template<class V>
    bool from_csv(const std::string_view& s, V& v, ErrorList& e, const std::string& path) const {
        try { v = static_cast<V>(std::stod(std::string(s))); return true; }
        catch (...) { e.add(path, "invalid number"); return false; }
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

} // namespace ds
