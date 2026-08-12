#include "SolveSnapshot.h"
#include "common/i18n/Language.h"

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace orchestration {
namespace {

/// 魔咒收集器：BFS 收集请求引用的魔咒及其 exclusive_set 闭包成员。
/// exclusive_set 是双向冲突关系——冲突矩阵两侧都需在场，快照的
/// EnchantmentRegistry 才能在插入时重建完整的 incompatibility_table。
/// 未知魔咒抛出（构建即校验）。
class EnchantCollector {
public:
    EnchantCollector(const EnchantmentRegistry& src, EnchantmentRegistry& out) : _src(src), _out(out) {}

    /// 收集魔咒（含 exclusive_set 闭包）。level >= 0 表示请求显式引用的等级
    /// ——构建即校验：超 max_level 抛出。闭包成员以 level = -1 传入（仅拉入
    /// 冲突矩阵，无等级语义，不校验）。lookup/校验先于 _seen 去重：即使某
    /// 魔咒先经闭包入快照，其后的显式请求等级仍会被校验。
    void collect(const NSID& id, int32_t level) {
        auto it = _src.find(id);
        if (it == _src.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_ench", id.str()));
        if (level > it->max_level)
            throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max", id.str(), level, it->max_level));
        if (_seen.count(id))
            return;
        _seen.insert(id);
        _out.insert(*it);
        for (const NSID& member : it->exclusive_set)
            collect(member, -1);
    }

private:
    const EnchantmentRegistry& _src;
    EnchantmentRegistry& _out;
    std::unordered_set<NSID> _seen;
};

} // namespace

SolveSnapshot build_solve_snapshot(const SolveRequest& request, const Profile& effective) {
    SolveSnapshot snap;

    // 1. 目标装备（书除外——书无装备定义）
    if (!request.target_item.is_book()) {
        auto eq_it = effective.eq().find(request.target_item.id);
        if (eq_it == effective.eq().end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", request.target_item.id.str()));
        snap.eq().insert(*eq_it);
    }

    // 2. 目标魔咒 + payload 魔咒（exclusive_set 闭包）；inventory 物品的
    //    装备定义一并收集（与目标装备路径同款校验）
    EnchantCollector collector(effective.ench(), snap.ench());
    for (const Ench& e : request.target_item.enchantments)
        collector.collect(e.id, e.level);
    if (const auto* direct = std::get_if<DirectPayload>(&request.payload)) {
        for (const Ench& e : direct->source_enchantments)
            collector.collect(e.id, e.level);
    } else if (const auto* inv = std::get_if<InventoryPayload>(&request.payload)) {
        for (const Item& item : inv->extra_items) {
            if (!item.is_book()) {
                auto eq_it = effective.eq().find(item.id);
                if (eq_it == effective.eq().end())
                    throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", item.id.str()));
                snap.eq().insert(*eq_it);
            }
            for (const Ench& e : item.enchantments)
                collector.collect(e.id, e.level);
        }
    }

    // 3. tag 子集：收集到的魔咒 supported_items 中的 `#` 引用
    const TagResolver* tr = effective.tag_resolver();
    std::unordered_set<std::string> collected_tags; // 去重：多魔咒可引用同一 tag
    for (const EnchInfo& info : snap.ench()) {
        for (const NSID& ref : info.supported_items) {
            if (!ref.is_tag())
                continue;
            const std::string key = ref.str().substr(1); // 去 '#' 前缀（resolver 键）
            if (!collected_tags.insert(key).second)
                continue;

            // TagRegistry 条目（存在则复制；缺失时 resolver 仍登记空成员）
            if (auto tag_it = effective.tags().find(ref); tag_it != effective.tags().end())
                snap.tags().insert(*tag_it);

            // 解析成员：优先 effective 解析器（get_tag 已 BFS 展开嵌套 tag）；
            // 无解析器（如手工构造的测试 Profile）回退 category 推导——
            // 语义对齐 SolvePipeline::fallback_tag_resolver。
            std::unordered_set<std::string> members;
            const auto colon = key.find(':');
            if (tr && colon != std::string::npos) {
                if (const auto* m = tr->get_tag(key.substr(0, colon), key.substr(colon + 1)))
                    members = *m;
            } else if (!tr) {
                for (const Equipment& eq : effective.eq())
                    if (eq.category == ref)
                        members.insert(eq.id.str());
            }
            snap.tag_resolver().add_tag(key, members);
        }
    }
    return snap;
}

} // namespace orchestration
