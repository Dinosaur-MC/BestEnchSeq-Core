#pragma once
#include "domain/business/components/TagResolver.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/Profile.h"
#include "domain/orchestration/types/SolveRequest.h"
#include <string>

namespace orchestration {

/// 按请求剪枝的有效视图快照（P0：solve 不再持有 ProfileManager 缓存引用）。
/// 只含请求引用的实体：魔咒（含 exclusive_set 闭包成员——冲突矩阵两侧都需
/// 在场）、装备（target/items）、上述魔咒 supported_items 引用的 tag 子集。
/// 构建即校验：未知魔咒/装备抛出。与 Profile 同形访问器（ench/eq/tags/tag_resolver）。
class SolveSnapshot {
public:
    SolveSnapshot() = default;
    const EnchantmentRegistry& ench() const noexcept { return _ench; }
    const EquipmentRegistry& eq() const noexcept { return _eq; }
    const TagRegistry& tags() const noexcept { return _tags; }
    const TagResolver& tag_resolver() const noexcept { return _resolver; }
    EnchantmentRegistry& ench() noexcept { return _ench; }
    EquipmentRegistry& eq() noexcept { return _eq; }
    TagRegistry& tags() noexcept { return _tags; }
    TagResolver& tag_resolver() noexcept { return _resolver; }

private:
    EnchantmentRegistry _ench;
    EquipmentRegistry _eq;
    TagRegistry _tags;
    TagResolver _resolver;
};

/// 构建器：遍历请求收集实体（exclusive_set 闭包 BFS；tag 子集按需填充）。
/// 未知魔咒/装备抛 std::runtime_error。profile 为有效视图（调用方保证 gate 内）。
SolveSnapshot build_solve_snapshot(const SolveRequest& request, const Profile& effective);

} // namespace orchestration
