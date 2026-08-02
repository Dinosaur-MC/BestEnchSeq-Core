#pragma once
#include "domain/business/types/Profile.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/// Manages Profile lifecycle: creation, activation, snapshot, branching, merging.
///
/// Built upon the concept of the former interface/ProfileSet, extended with
/// snapshot, branch, version metadata, and NSID-based naming.
class ProfileManager {
public:
    ProfileManager() = default;

    // ── CRUD ──────────────────────────────────────────────────────────

    /// Create an empty profile.
    Profile& create(const NSID& name);

    /// Create a profile from an existing source (deep copy).
    Profile& create_from(const NSID& source, const NSID& dest);

    /// Remove a profile. Returns false if not found.
    bool remove(const NSID& name);

    /// Check if a profile exists.
    bool exists(const NSID& name) const;

    /// Find profile by name (nullptr if not found).
    Profile* find(const NSID& name);
    const Profile* find(const NSID& name) const;

    /// List all profile names.
    std::vector<NSID> list() const;

    /// Number of managed profiles.
    size_t size() const noexcept { return _profiles.size(); }

    // ── Stable CRUD (real-time validation + auto snapshot/undo) ────────

    /// Add an enchantment to a profile.  Real-time validation: the profile must
    /// be valid before the change and stay valid after; a failing edit leaves
    /// the profile untouched and records no snapshot.  Successful changes are
    /// rolled back by undo().
    bool add_enchantment(const NSID& profile, const EnchInfo& info);
    bool update_enchantment(const NSID& profile, const EnchInfo& patch);
    bool remove_enchantment(const NSID& profile, const NSID& id);
    bool add_equipment(const NSID& profile, const Equipment& eq);
    bool remove_equipment(const NSID& profile, const NSID& id);
    bool add_tag(const NSID& profile, const EquipmentTag& tag);
    bool remove_tag(const NSID& profile, const NSID& id);

    /// Roll back the most recent successful manager-level change to a profile.
    /// Returns false if there is nothing to undo for the profile.
    bool undo(const NSID& profile);

    // ── Activation ────────────────────────────────────────────────────

    /// Set the active profile. Throws std::runtime_error if not found.
    void activate(const NSID& name);

    /// Get active profile. Throws std::runtime_error if manager is empty.
    Profile& active();
    const Profile& active() const;

    /// Name of the active profile.
    const NSID& active_name() const noexcept { return _active; }

    // ── Snapshot ──────────────────────────────────────────────────────

    /// Create an immutable point-in-time copy of a profile.
    Profile& snapshot(const NSID& source, const NSID& snapshot_name);

    // ── Branch ────────────────────────────────────────────────────────

    /// Create an independently evolvable fork of a profile.
    /// The branch inherits all data and tracks parent metadata.
    Profile& branch(const NSID& source, const NSID& branch_name);

    // ── Merge ─────────────────────────────────────────────────────────

    /// Merge source profile into dest (insert_or_assign semantics).
    /// Source entries overwrite dest entries on conflict.
    void merge(const NSID& source, const NSID& dest);

    // ── Dependency graph ──────────────────────────────────────────────

    /// 传递解析依赖链（拓扑序：依赖在前，不含目标自身）。环 → 返回空。
    std::vector<NSID> resolve_dependencies(const NSID& profile) const;

    /// 拓扑合并依赖链 + 自身 → 有效视图 Profile（缓存；上层覆盖下层）。
    /// 任何 profile 变更（manager 级 mutation）都会使缓存失效。
    const Profile& resolve_effective(const NSID& profile) const;

    /// 从目录加载全部 profile（native JSON/CSV + datapack 子目录），构建依赖图。
    void load_directory(const std::filesystem::path& dir);

    /// 从 MC datapack 目录加载 profile（要求 dir/pack.mcmeta 存在）。
    /// 通过 McOfficialParser 解析 data/*/enchantment + tags，经与 ProfileLoader
    /// 相同的两阶段 RegistryLoader 路径构建，仅保留 datapack 自身内容。
    /// 返回 false 表示目录不是有效 datapack 或解析失败。
    bool load_datapack(const std::filesystem::path& dir);

    /// 对目标 profile 的 supported_items 引用按 (vanilla ∪ 依赖链) 交叉验证，返回移除数。
    size_t cross_validate(const NSID& profile);

    /// 显式使有效视图缓存失效。直接修改 Profile（绕过 manager 级 mutation，
    /// 如 BesqContext::load_file/import_registry 的批量合并）后必须调用，否则
    /// resolve_effective 会返回陈旧视图。
    void notify_mutated() const { _effective_cache.clear(); }

    // ── Publish ────────────────────────────────────────────────────────

    /// 版本化发布：拍平有效视图为自包含 profile 文件（内嵌 version/tag）。
    bool publish(const NSID& profile, const std::string& version,
                 const std::string& tag, const std::filesystem::path& out);

private:
    Profile* _find(const NSID& name);
    const Profile* _find(const NSID& name) const;

    /// Apply `op` to a profile under real-time validation + snapshot.
    /// Steps: validate-before → snapshot → apply → validate-after (rollback on
    /// failure).  Returns true only when the change is applied and valid.
    bool _mutate(const NSID& profile, std::function<bool(Profile&)> op);

    /// Rebuild the adjacency list from the current profiles. `mutable` so a
    /// const resolve_dependencies() can honor direct set_dependencies() calls.
    void _build_graph() const;

    struct Snapshot {
        Json before;  // pre-change profile state (Json round-trip)
    };
    std::unordered_map<NSID, std::vector<Snapshot>> _undo_log;  // 每个 profile 的变更历史

    std::unordered_map<NSID, std::unique_ptr<Profile>> _profiles;
    mutable std::unordered_map<NSID, std::vector<NSID>> _dep_graph;  // 邻接表
    mutable std::unordered_map<NSID, std::unique_ptr<Profile>> _effective_cache;
    NSID _active;
};
