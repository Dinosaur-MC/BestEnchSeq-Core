#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/algorithm/types/AlgorithmState.h"
#include "domain/business/sql/SqlExecutor.h"   // business::sql::SqlResult（run_sql 返回类型）
#include "domain/orchestration/orchestration.h"
#include "domain/orchestration/types/SolveSnapshot.h"

namespace algorithm {
class IExecutor;
}

// ── Algorithm detail / unload (API value types) ────────────────────────
enum class AlgorithmOrigin { builtin, plugin };

struct AlgorithmDetail {
    std::string name;
    std::string version;
    AlgorithmOrigin origin = AlgorithmOrigin::builtin;
    std::string plugin_path; ///< plugin 才有
    bool is_resumable = false;
    std::string supported_mode; ///< "direct" / "inventory"
    bool has_audit = false;
    /// IAlgorithm::evaluate(16) 预估耗时（秒）；算法无法就地实例化时为空
    /// （如沙箱模式插件——父进程从不 dlopen，create() 返回 nullptr）。
    std::optional<double> predicted_sec;
};

/// Named-profile metadata snapshot (profile_metadata 返回）。
struct ProfileMeta {
    std::string name;
    std::vector<std::string> dependencies;
    std::string description; ///< 概要描述（vanilla.json 顶层）
    std::string version;     ///< 空 = 未发布
    std::string mc_version;  ///< 目标 Minecraft 版本 id（如 "26.2"；空 = 未声明）
    std::string release_tag; ///< 空 = 未发布
    bool is_root = false;
    size_t ench_count = 0, eq_count = 0, tag_count = 0;
    std::string format; ///< native_json / csv / datapack / builtin
};

// ── SolveHistory（求解历史，计划 B Task B2）────────────────────────
/// 求解生命周期事件类型。
enum class SolveEventType : uint8_t { Submitted, Completed, Failed, Cancelled };

/// 一次求解生命周期事件（绑定 BesqContext，每 context 有界环形覆盖，线程安全）。
struct SolveHistoryEvent {
    uint64_t seq = 0; ///< context 内单调递增（record_solve_event 赋值，从 1 起）
    SolveEventType type = SolveEventType::Submitted;
    std::string task_id;      ///< web 任务 id；CLI/ABI 求解为空串
    std::string target;       ///< 目标物品摘要（如 minecraft:diamond_sword[minecraft:sharpness=5]）
    std::string algorithm;    ///< 使用的算法
    std::string mode;         ///< direct / inventory
    int64_t timestamp_ms = 0; ///< 事件时间（wall clock ms；0 → 记录时填充）
    // Completed 附加
    int64_t total_level_cost = 0; ///< 最佳方案的附魔台等级总成本
    int64_t total_exp_cost = 0;   ///< 最佳方案的经验总成本
    int64_t solution_count = 0;
    int64_t computation_ms = 0; ///< 求解耗时（= SolveResult::computation_time_ms，三处同源）
    std::string result_json;    ///< 完整结果 JSON（仅 Completed 填充；其余类型空串）
    // Failed 附加
    std::string error_message;
};

/// 每 context 求解历史上限（有界环形覆盖最旧）。
inline constexpr size_t kMaxSolveHistory = 100;

/// 目标物品紧凑摘要（如 `minecraft:diamond_sword[minecraft:sharpness=5]`）。
/// 供求解历史事件使用；空附魔集合时形如 `minecraft:diamond_sword[]`。
inline std::string solve_target_summary(const Item& item) {
    std::string s = item.id.str();
    s += "[";
    bool first = true;
    for (const auto& e : item.enchantments) {
        if (!first)
            s += ",";
        s += e.id.str() + "=" + std::to_string(e.level);
        first = false;
    }
    s += "]";
    return s;
}

/// Main public API class for BestEnchSeq.
/// Session facade: holds session state (active profile, algorithm loader) and
/// exposes service interfaces.  Management / export / formatting logic is
/// delegated to the orchestration pipelines (ManagePipeline / ExportPipeline);
/// solving, read-only registry access, and algorithm queries stay direct.
class BesqContext {
public:
    BesqContext();
    ~BesqContext();

    BesqContext(const BesqContext&) = delete;
    BesqContext& operator=(const BesqContext&) = delete;
    BesqContext(BesqContext&&) noexcept;
    BesqContext& operator=(BesqContext&&) noexcept;

    // ── Profile lifecycle ──
    void load_builtin();
    size_t load_algorithms(const std::string& dir_path);
    void load_file(const std::string& path);
    void load_data(const std::vector<std::string>& filters);

    /// Domain-wide auto-load (the CLI / HTTP service startup entry): built-in
    /// first, then external profiles / algorithm plugins / on-disk languages
    /// — order + conflict rules are owned by orchestration::AutoLoadPipeline
    /// (profiles replace-on-conflict, algorithms new-version-wins, langs
    /// set-union merge).  Defaults are exe-relative; set_profiles_dir() and
    /// BESQ_ALGO_DIR overrides are honored.  Safe no-op for missing dirs.
    void auto_load();

    /// Override the default profiles directory (default: `<cwd>/profiles`).
    void set_profiles_dir(const std::string& dir);

    /// Scan the profiles directory (set_profiles_dir, or `<cwd>/profiles`) and
    /// load every native JSON/CSV profile into the manager.  No-op if the
    /// directory does not exist.
    void load_profiles();

    // ── Profile management ──
    std::string active_profile() const noexcept;
    /// 当前激活的 profile 成员列表（组合 = 成员；单 profile = {name}）。
    std::vector<std::string> active_profiles() const;
    /// 当前是否组合模式（≥2 个成员）。
    bool composite_active() const noexcept;
    std::vector<std::string> list_profiles() const;
    void activate_profile(const std::string& name);
    /// 激活 profile 组合（成员按给定次序，后覆盖前；隐式 vanilla base）。
    /// 成员不存在或任一成员处于依赖环 → throw std::runtime_error。
    /// 空列表 = 清除组合（回退单 profile 模式）。≤1 个成员 = 单 profile 激活。
    void activate_profile_group(std::vector<std::string> members);
    void fork_profile(const std::string& source, const std::string& dest);
    void merge_profile(const std::string& source, const std::string& dest);
    void remove_profile(const std::string& name);

    /// Versioned publish: flatten the profile's effective view (incl. deps)
    /// into a self-contained profile file (embeds version/tag).  Delegates to
    /// ProfileManager::publish.  Returns false if the profile is unknown or the
    /// file cannot be written.
    bool
    publish_profile(const std::string& name, const std::string& version, const std::string& tag, const std::string& out_path);

    // ── Profile data access (by name, no activation side effect) ──
    /// The named profile's OWN data (raw registries; inherited dependency
    /// content is NOT merged in). Throws std::runtime_error when the profile
    /// is unknown. Does NOT change the active profile. Callers needing the
    /// dependency-merged view should use effective_profile().
    const Profile& profile(const std::string& name) const;

    /// Dependency-merged effective view of the named profile (mirrors what
    /// solve consumes: ProfileManager::resolve_effective). Throws
    /// std::runtime_error on unknown profile / dependency cycle. Reference is
    /// stable while the manager is not mutated — controllers must hold the
    /// context gate.
    const Profile& effective_profile(const std::string& name) const;

    // ── Registry editing (named profile) ──
    /// Stable by-name CRUD forwarded to ProfileManager's validated `_mutate`
    /// path (real-time validation + snapshot/undo). Operates on ANY profile,
    /// not just the active one.
    bool add_enchantment_to(const std::string& profile, const EnchInfo& info);
    bool remove_enchantment_from(const std::string& profile, const NSID& id);
    bool add_equipment_to(const std::string& profile, const Equipment& eq);
    bool remove_equipment_from(const std::string& profile, const NSID& id);
    bool add_tag_to(const std::string& profile, const EquipmentTag& tag);
    bool remove_tag_from(const std::string& profile, const NSID& id);
    /// Update variants — delegate to ProfileManager::update_*. Operates on ANY
    /// profile. Returns false if the profile/entry is unknown or the edit
    /// leaves the profile invalid.
    bool update_enchantment_to(const std::string& profile, const EnchInfo& patch);
    bool update_equipment_to(const std::string& profile, const Equipment& patch);
    bool update_tag_to(const std::string& profile, const EquipmentTag& patch);

    // ── Profile metadata & rename ──
    /// Existence check without throwing.
    bool profile_exists(const std::string& name) const;
    /// Snapshot the named profile's metadata + registry sizes. Unknown → throw
    /// std::runtime_error.
    ProfileMeta profile_metadata(const std::string& name) const;
    /// Rename a profile (active name follows). False if old unknown / new taken.
    bool rename_profile(const std::string& old_name, const std::string& new_name);
    /// Set a profile's dependency list. False if the profile is unknown or the
    /// change would create a dependency cycle (no mutation in either case).
    bool set_dependencies(const std::string& profile, std::vector<std::string> deps);

    // ── Registry editing (active profile) ──
    bool add_enchantment(const EnchInfo& info);
    bool remove_enchantment(const std::string& name_id);
    bool modify_enchantment(const std::string& name_id, const EnchInfo& patch);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const std::string& name_id);
    bool add_category(const std::string& name);

    // ── Profile data import / export ──
    void import_profile(const std::string& path);
    bool export_profile(const std::string& path) const;
    /// Export the active profile's data as a string (in-memory JSON), without
    /// writing a file.  Used by `--export -` (stdout dump).
    std::string export_profile_to_string() const;

    // ── Profile SQL（profile sql，片 1 链式模式）────────────────────────
    /// 一次 profile sql 链式执行的结果。
    /// - `last`：最后一条语句的结果（链中止 = 失败语句的结果；headers 空 =
    ///   写/STATUS/SAVE 语句，消息在 `steps` 中）。
    /// - `steps`：每条已执行语句的结果（顺序；含失败语句），CLI 逐条打印消息。
    /// - `dirty`：链结束后的脏 profile 名（排序），供 CLI 退出警告
    ///   （选择：随结果返回，而非持久会话 + sql_dirty_profiles() 访问器——
    ///   片 1 CLI 每进程一次调用，会话状态无需跨调用保留）。
    struct SqlRunResult {
        business::sql::SqlResult last;
        std::vector<business::sql::SqlResult> steps;
        std::vector<std::string> dirty;
    };

    /// 链式执行多条 SQL 语句（分号分隔）：先整体解析（解析失败 → \p error
    /// 非空、零执行）；再逐条执行，语句错误中止链（\p error = "statement N
    /// failed: <消息>"），**之前成功的语句保持生效**（无跨语句回滚）。
    /// \p profile 为会话初始工作 profile（空 = 不切换；未知 → \p error 非空）。
    /// 返回最后一条语句的 SqlResult + 逐步结果 + 脏 profile。会话为调用内
    /// 新建（自包含），数据源 = 本 context 的私有 ProfileManager +
    /// profiles_dir（set_profiles_dir 覆盖 > AppConfig::get().profiles_dir >
    /// 默认 <exe_dir>/profiles，与 load_profiles() 同解析）。
    SqlRunResult run_sql(const std::string& statements, const std::string& profile, std::string& error);

    // ── Registry access (active profile, read-only) ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const TagRegistry& categories() const noexcept;

    /// Live progress of the in-flight solve, read via the atomic executor
    /// handle. Idle/0.0 when no solve is running. Designed for the Web API's
    /// polling loop; thread-safe (reads the atomic shared_ptr handle).
    struct SolveProgress {
        algorithm::AlgorithmState state = algorithm::AlgorithmState::Idle;
        double progress = 0.0;
    };
    SolveProgress solve_progress() const noexcept;

    // ── Solve ──
    /// P0：gate 内按请求构建剪枝快照（含全部校验）；solve 跑在快照上。
    /// 未知魔咒/装备抛 std::runtime_error。
    orchestration::SolveSnapshot solve_snapshot(const SolveRequest& request) const;

    /// P0：无 gate——管线消费快照，不触碰 ProfileManager。快照由
    /// solve_snapshot()（gate 内）先行构建，solve 全程零 profile 引用。
    SolveResult solve(const SolveRequest& request, const orchestration::SolveSnapshot& snapshot);

    /// 单参包装（CLI/ABI 单线程路径，无 gate 竞争）：内部 resolve +
    /// build_snapshot + 双参 solve。
    SolveResult solve(const SolveRequest& request);
    void abort_solve();

    // ── Solve history（求解历史）──
    /// 记录一条求解事件：内部赋 seq（从 1 单调递增）+ timestamp_ms（若为 0）
    /// + 有界入队（超 kMaxSolveHistory 覆盖最旧）。独立小锁，不占用业务
    /// gate——CLI/ABI 主线程与 web worker 并发写、/api/history 读。
    void record_solve_event(SolveHistoryEvent ev);

    /// 快照拷贝全部事件，最新在前（锁内拷贝，容量上限 kMaxSolveHistory）。
    std::vector<SolveHistoryEvent> solve_history() const;

    /// Pause the in-flight solve at its next pause point (batch C). Follows the
    /// abort_solve pattern: copies the atomic executor handle and calls
    /// IExecutor::pause() (which returns once the algorithm has quiesced). No-op
    /// when nothing is running / the handle is not yet published.
    void pause_solve();

    /// Resume a paused solve (batch C): IExecutor::resume() on the live handle.
    /// No-op when nothing is running.  Pause/resume only — checkpoint
    /// persistence/restore are separate APIs (save_solve_state /
    /// solve_from_checkpoint).
    void resume_solve();

    /// Serialize the paused in-flight solve into \p path (a binary checkpoint
    /// blob).  Valid only while the solve is Paused — serialize_state() waits
    /// for algorithm quiescence; non-serializable algorithms or an empty blob
    /// return false.  Callers pick the file (HTTP endpoint / CLI tooling).
    bool save_solve_state(const std::string& path);

    /// Result of solve_from_checkpoint: the solve result plus the algorithm
    /// mode recovered from the checkpoint's input (CLI formatting needs it).
    struct CheckpointSolveResult {
        SolveResult result;
        AlgorithmMode mode;
    };

    /// Resume a computation from a checkpoint file (CLI --resume / HTTP
    /// service restore endpoint).  Reads the blob, peeks its algorithm tag via
    /// IExecutor::peek, creates the executor and calls start(blob) — no
    /// target/source re-specification needed.  Publishes the active executor
    /// handle during the run (abort/pause work as for solve()).  Throws on
    /// invalid checkpoint / unknown algorithm.
    CheckpointSolveResult solve_from_checkpoint(const std::string& path);

    // ── Format ──
    std::string format(const SolveResult& result, AlgorithmMode mode, std::string_view fmt) const;

    // ── Algorithm queries ──
    std::vector<std::string> list_algorithms() const;

    /// Per-strategy metadata (origin/version/mode/resume/audit/path).
    /// Unknown name → throw std::runtime_error.
    AlgorithmDetail algorithm_detail(const std::string& name) const;
    /// Unload a plugin by name. Built-in / unknown → false (never unloads the
    /// trusted kernel). Plugin instances must already be destroyed.
    bool unload_algorithm(const std::string& name);

private:
    /// 当前激活的有效视图：组合时 = resolve_effective_group(成员)，否则 =
    /// resolve_effective(active_name())。所有求解/查看/导出消费点统一走这里。
    const Profile& _resolve_active() const;

    struct Impl;
    std::unique_ptr<Impl> _impl;
};
