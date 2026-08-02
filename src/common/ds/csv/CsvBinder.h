#pragma once
#include "common/io/CsvIO.h"
#include "ds/Error.h"
#include "ds/Field.h"
#include "ds/codec/Codecs.h"

#include <cstddef>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace ds::csv {

/// 物理 CSV 绑定：S 为逻辑 schema。CSV 序列化所有字段（忽略发射谓词，
/// 列固定）；header 按字段声明顺序。parse_row 按 header 列名→索引定位。
template<typename S>
struct Schema {
    using Type = typename S::Type;
    static constexpr auto fields = S::fields;

    static ::csv::CsvRow header() {
        ::csv::CsvRow hdr;
        std::apply([&](const auto&... f) { ((hdr.push_back(f.name)), ...); }, fields);
        return hdr;
    }
    static ::csv::CsvRow serialize_row(const Type& o) {
        ::csv::CsvRow row;
        std::apply([&](const auto&... f) {
            ((row.push_back(cell_of(f, o)), ...));
        }, fields);
        return row;
    }
    template<typename F>
    static std::string cell_of(const F& f, const Type& o) {
        std::string cell;
        f.codec.to_csv(f.get(o), cell);
        return cell;
    }
    static bool parse_row(const ::csv::CsvRow& hdr, const ::csv::CsvRow& row,
                          Type& o, ErrorList& err) {
        std::unordered_map<std::string, std::size_t> col;
        for (std::size_t i = 0; i < hdr.size(); ++i)
            col[hdr[i]] = i;
        bool ok = true;
        std::apply([&](const auto&... f) { (parse_field(f, col, row, o, err, ok), ...); }, fields);
        return ok && err.empty();
    }
    template<typename F>
    static void parse_field(const F& f, const std::unordered_map<std::string, std::size_t>& col,
                            const ::csv::CsvRow& row, Type& o, ErrorList& err, bool& ok) {
        auto it = col.find(f.name);
        if (it == col.end() || it->second >= row.size()) {
            if (f.required) { err.add(f.name, "missing required column"); ok = false; }
            return;
        }
        typename F::value_type v{};
        if (f.codec.from_csv(row[it->second], v, err, f.name)) f.set(o, std::move(v));
        else ok = false;
    }
};

} // namespace ds::csv
