#include "DatapackExporter.h"
#include "common/io/json.h"
#include "domain/business/types/Profile.h"
#include "domain/business/components/TagResolver.h"
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace orchestration {

namespace {

constexpr int64_t kPackFormat = 61;   // MC 1.21+

/// profile key 的 NSID 名空间（含 ':' 取前缀；否则 key 本身）。
std::string profile_ns(const Profile& p) {
    const std::string& name = p.name();
    const auto colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(0, colon);
}

bool write_file(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) { error = "cannot write " + path.string(); return false; }
    out << content;
    if (!out) { error = "write failed: " + path.string(); return false; }
    return true;
}

/// NSID → 文件名尾部：去 '#'、去 ns 前缀、去 '/' 子路径。
std::string tail_of(const NSID& id) {
    std::string s = id.str();
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    const auto colon = s.find(':');
    if (colon != std::string::npos) s = s.substr(colon + 1);
    const auto slash = s.rfind('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    return s;
}

} // namespace

bool DatapackExporter::export_profile(const Profile& profile,
                                      const std::filesystem::path& dir,
                                      std::string& error) {
    // ── 目标检查：已存在且非空 → 拒绝；创建目录树 ──
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::is_directory(dir, ec))
            { error = "not a directory: " + dir.string(); return false; }
        for (auto it = std::filesystem::directory_iterator(dir, ec);
             !ec && it != std::filesystem::directory_iterator(); ++it) {
            { error = "directory not empty: " + dir.string(); return false; }
        }
    }
    std::filesystem::create_directories(dir, ec);
    if (ec) { error = "cannot create " + dir.string() + ": " + ec.message(); return false; }

    const std::string ns = profile_ns(profile);
    const auto ench_dir   = dir / "data" / ns / "enchantment";
    const auto tag_item_dir = dir / "data" / ns / "tags" / "item";
    const auto tag_ench_dir = dir / "data" / ns / "tags" / "enchantment";
    std::filesystem::create_directories(ench_dir, ec);
    std::filesystem::create_directories(tag_item_dir, ec);
    std::filesystem::create_directories(tag_ench_dir, ec);
    if (ec) { error = "cannot create data dirs"; return false; }

    // ── pack.mcmeta ──
    {
        Json o = Json::object();
        Json pack = Json::object();
        pack["pack_format"] = Json(kPackFormat);
        const std::string desc = profile.metadata().description.empty()
            ? profile.name() : profile.metadata().description;
        pack["description"] = Json(desc);
        o["pack"] = std::move(pack);
        if (!write_file(dir / "pack.mcmeta", o.to_string(), error)) return false;
    }

    // ── data/<ns>/enchantment/<tail>.json ──
    std::vector<std::string> treasure_ids;
    for (const auto& e : profile.ench()) {
        const std::string full = e.id.str();
        const auto colon = full.find(':');
        const std::string tail = colon == std::string::npos ? full : full.substr(colon + 1);
        Json o = Json::object();
        o["anvil_cost"] = Json(static_cast<int64_t>(e.multiplier));
        o["max_level"]  = Json(static_cast<int64_t>(e.max_level));
        Json excl = Json::array();
        for (const auto& x : e.exclusive_set) excl.push_back(Json(x.str()));
        o["exclusive_set"] = std::move(excl);
        Json supp = Json::array();
        for (const auto& s : e.supported_items) supp.push_back(Json(s.str()));
        o["supported_items"] = std::move(supp);
        Json mc = Json::object();
        mc["base"] = Json(static_cast<int64_t>(e.min_cost_base));
        mc["per_level_above_first"] = Json(static_cast<int64_t>(e.min_cost_per_level));
        o["min_cost"] = std::move(mc);
        if (e.limited_level_provided)
            o["limited_level"] = Json(static_cast<int64_t>(e.limited_level));
        if (e.is_treasure) treasure_ids.push_back(full);
        if (!write_file(ench_dir / (tail + ".json"), o.to_string(), error)) return false;
    }

    // ── data/<ns>/tags/item/<category>.json（装备按类别分组；值 = 完整 NSID）──
    std::unordered_map<std::string, std::vector<std::string>> by_cat;
    for (const auto& e : profile.eq()) by_cat[tail_of(e.category)].push_back(e.id.str());
    for (const auto& [cat, ids] : by_cat) {
        Json o = Json::object();
        Json arr = Json::array();
        for (const auto& id : ids) arr.push_back(Json(id));
        o["values"] = std::move(arr);
        if (!write_file(tag_item_dir / (cat + ".json"), o.to_string(), error)) return false;
    }

    // ── TagRegistry 定义 → tags/item/<tail>.json（values 取 resolver 解析集）──
    // （类别标签文件可能被此步以 resolver 值为准覆盖——成员关系真值在 resolver。）
    if (const auto* res = profile.tag_resolver()) {
        for (const auto& t : profile.tags()) {
            std::string s = t.id.str();
            if (s.empty() || s[0] != '#') continue;          // 只导出标签型定义
            s = s.substr(1);
            const auto colon = s.find(':');
            if (colon == std::string::npos) continue;
            const std::string ns2 = s.substr(0, colon);
            const std::string name = s.substr(colon + 1);
            if (name.find('/') != std::string::npos) continue;   // 子路径（enchantment/…）不写 item 标签
            const auto* vals = res->get_tag(ns2, name);
            if (!vals) continue;
            Json o = Json::object();
            Json arr = Json::array();
            for (const auto& v : *vals) arr.push_back(Json(v));
            o["values"] = std::move(arr);
            if (!write_file(tag_item_dir / (name + ".json"), o.to_string(), error)) return false;
        }
    }

    // ── data/<ns>/tags/enchantment/treasure.json（is_treasure 保真）──
    //    ns != minecraft 且有宝藏 → 另写 data/minecraft/tags/enchantment/treasure.json
    //   （解析器经 #minecraft:treasure 判定宝藏成员）
    if (!treasure_ids.empty()) {
        Json o = Json::object();
        Json arr = Json::array();
        for (const auto& id : treasure_ids) arr.push_back(Json(id));
        o["values"] = std::move(arr);
        if (!write_file(tag_ench_dir / "treasure.json", o.to_string(), error)) return false;
        if (ns != "minecraft") {
            const auto mc_dir = dir / "data" / "minecraft" / "tags" / "enchantment";
            std::filesystem::create_directories(mc_dir, ec);
            if (ec) { error = "cannot create minecraft tag dir"; return false; }
            if (!write_file(mc_dir / "treasure.json", o.to_string(), error)) return false;
        }
    }

    return true;
}

} // namespace orchestration
