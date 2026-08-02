# Algorithm Diagnostics 规范（内置 + 插件统一）

**适用对象：** 所有 `IAlgorithm` 实现 —— 内置策略与 `plugins/` 下的插件算法一视同仁。
**目标：** 让每个算法在几乎不损失性能的前提下，记录"valgrind 给不了的搜索语义指标"，为跨算法对比与未来插件生态提供一个稳定、统一的诊断规范与模板。
**关联：** 规范摘要见 `src/domain/algorithm/IAlgorithm.h` 的《算法策略设计核心规范》；本文件为完整版。

---

## 目录

1. [为什么要这份规范](#1-为什么要这份规范)
2. [信息维度划分：valgrind vs 诊断系统](#2-信息维度划分valgrind-vs-诊断系统)
3. [诊断分类学](#3-诊断分类学)
4. [公共核心（必填）](#4-公共核心必填)
5. [性能分层（Tier 0 / 1 / 2）](#5-性能分层tier-0--1--2)
6. [命名规范](#6-命名规范)
7. [Schema 版本](#7-schema-版本)
8. [模板：PartitionDpDiagnostics](#8-模板partitiondpdiagnostics)
9. [插件落地指引（step-by-step）](#9-插件落地指引step-by-step)
10. [参考：valgrind 实验数据](#10-参考valgrind-实验数据)

---

## 1. 为什么要这份规范

本项目是一个**可扩展的算法平台**：除了内置 5 个策略，还有 `plugins/` 下的第三方算法（当前有 `diff_first`、`idastar`、`penalty_balance`），未来会有更多开发者共同编制算法。诊断系统要同时服务：

- **当下**：内置算法的搜索健康度分析（为什么慢、剪枝是否生效、状态空间多大）。
- **未来**：插件作者写新算法时，能照着统一规范低成本地记录自己的语义指标，且字段名、粒度、性能影响有章可循，工具（benchmark、CLI、分析脚本）能长期稳定消费。

没有统一规范时，每个算法各自发明字段（如 `tt_lookups`）、各自埋热路径计数器，既无法跨算法对比，又可能引入性能回归。

## 2. 信息维度划分：valgrind vs 诊断系统

两者**不重叠**，各司其职：

| | valgrind（cachegrind/callgrind/massif） | 诊断系统（AlgorithmDiagnostics） |
|---|---|---|
| 维度 | CPU/硬件级：每函数指令数、cache miss、分支、分配栈 | 搜索/算法级：状态空间、剪枝、memo、进度、阶段 |
| 粒度 | 函数/指令 | 子问题/分区/阶段 |
| 时机 | 事后快照，**无法看进度/取消/阶段** | 运行中 + 结束，可报进度、Pass 拆分 |
| 计时 | 被 hook 放大（不准） | 真实 wall_ms |
| 成本 | 30–100x 减速，不可能常规跑 | 设计为近零（见 §5）|
| 归因 | Ir 无法归因到"被剪掉的工作" | 算法自己知道每个剪枝 |

**结论**：valgrind 管微观（哪个函数烧指令），诊断管宏观（搜索空间/剪枝/进度）。诊断系统的定位是**补 valgrind 的盲区**，不是重复它的指令统计。

### valgrind 给不了、诊断该给的关键指标

1. **状态空间大小**（子问题数 / memo cache 占用）——cachegrind 只有 `solve` 的总 Ir，分不清命中 vs 重算。
2. **memo cache 命中率**——valgrind 看的是 CPU cache（D1mr），不是算法的 memo map。
3. **剪枝有效性**（cap / bound / Pareto 各剪掉多少）——Ir 无法归因到"被剪掉的工作"。
4. **进度 / 取消 / Pass A/B 拆分**——事后快照看不到。
5. **跨算法可比的状态数**（`normalized_explored_states`）——回答"为什么 A 比 B 慢"。
6. **领域结构**（步长分布、UB 与最优的差距、顶部 frontier 大小）。

## 3. 诊断分类学

按**搜索范式**分 4 类，插件对号入座：

```
AlgorithmDiagnostics                ← 公共核心（所有算法）
├─ SearchDiagnostics                ← 展开式搜索（dfs、未来搜索插件）
│   └─ PoolSearchDiagnostics        ← 用 ItemPool 的搜索（astar、idastar）
└─ PartitionDpDiagnostics           ← Catalan/分治 DP（bb_dp、dp_merge、未来 DP 插件）
    └─ 可再派生（如 BBoundDpDiagnostics : PartitionDpDiagnostics）
```

选型依据（与 `IAlgorithm.h` §4 一致）：

| 范式 | 选型 | 例子 |
|---|---|---|
| 确定性合成（贪心、无搜索）| `AlgorithmDiagnostics` | hamming、diff_first、penalty_balance |
| 搜索，无 ItemPool | `SearchDiagnostics` | dfs |
| 搜索，有 ItemPool | `PoolSearchDiagnostics` | astar、idastar |
| Catalan/分治 DP（含可选 B&B bound）| `PartitionDpDiagnostics` | bb_dp、dp_merge |

## 4. 公共核心（必填）

所有算法必须通过 `ctx.set_exit_diagnostics(_diag)` 上报（框架自动补 algorithm_name / wall_ms）：

| 字段 | 说明 |
|---|---|
| `status` | `"Complete"` / `"Cancelled"` / `"CompleteNoSolution"` 等 |
| `solution_cost` | 最优/最终解成本，无解为 -1 |
| `diag_schema_version` | 诊断 schema 版本（当前 1）|
| `normalized_explored_states` | **跨算法可比的状态数**：搜索类填 `explored_count`，DP 类填 `subproblems_solved`。这是"为什么 X 慢"的第一对比指标 |

`normalized_explored_states` 是关键设计：把不同范式"探索了多少状态"统一到一个字段，`explored_count` 与 `subproblems_solved` 直接可比。

## 5. 性能分层（Tier 0 / 1 / 2）

**铁律：每操作级原子计数器禁止无条件使用。** 已实测（见 §10）：relaxed 原子没有 fence，但有 **cacheline 争抢**；sword_16 上 203M 次 `incr_steps_forged()` ≈ 1.5s。规范按粒度分三层：

| 层 | 粒度 | 成本 | 规则 |
|---|---|---|---|
| **Tier 0** | pass 结束时派生（扫描已有状态，零计数器）| 零 | 无条件可用。例：扫描 memo cache 得到子问题数 / 命中率 / 最大 frontier |
| **Tier 1** | 每状态/每子问题（≤ 2^n，n≤20 即 ≤ 1M 次）| 可忽略（~ms）| 可无条件加。原子操作数 ≤ 2^n |
| **Tier 2** | 每操作（每 forge / 每展开）| **贵** | **必须**门控在 `BESQ_DEEP_DIAGNOSTICS` 后（默认关，编译期零成本），并注明性能影响 |

**Tier 1 的聚合模式**：不要在每个 forge/展开处 `atomic++`，而是**在单个状态内用本地非原子累加，该状态结束时一次性 flush 到全局原子**。这样原子操作数从"操作数（203M）"降到"状态数（2^n ≤ 1M）"。

```cpp
// ❌ Tier 2（每操作原子）：仅当 BESQ_DEEP_DIAGNOSTICS 定义时启用，
//    否则 `incr_*` 是空内联函数，编译器消除全部调用点 → 真正零成本
#if defined(BESQ_DEEP_DIAGNOSTICS)
    ctx.incr_steps_forged();
#endif

// ✅ Tier 1：状态内本地累加，状态结束 flush 一次（≤2^n 次原子）
uint64_t local_pruned = 0;
for (/* 状态内的操作 */) { if (pruned) ++local_pruned; }
_dp_pruned.fetch_add(local_pruned, std::memory_order_relaxed);
```

### 门控宏总览（CMake option，两者正交）

| 宏 | 默认 | 控制 | 构建 |
|---|---|---|---|
| `BESQ_DISABLE_DIAGNOSTICS` | OFF | 编译掉诊断**输出**（`DiagnosticsWriter` 持久化到 logs/diag）| `-DBESQ_DISABLE_DIAGNOSTICS=ON` |
| `BESQ_DEEP_DIAGNOSTICS` | OFF | 启用每操作**计数器**（`ExecutionContext::incr_*`）| `-DBESQ_DEEP_DIAGNOSTICS=ON` |

默认构建（两者皆 OFF）：诊断输出正常，每操作计数器**零成本**（空内联函数，调用点被编译器消除）。深挖单次运行需要 `-DBESQ_DEEP_DIAGNOSTICS=ON` 重建一个 profiling 构建——不能运行时热切换，这是"编译期零成本"的代价。

## 6. 命名规范

字段名 `snake_case`，**带范式/插件前缀**，防碰撞且便于工具归类：

```
dp_subproblems_solved, dp_cache_hits, dp_pareto_dropped     // DP 类
search_explored, search_pruned_by_bound, search_max_depth    // 搜索类
idastar_tt_lookups, idastar_tt_stores                        // 插件专属前缀
```

前缀命名：`<范式缩写>_`（内置）；`<插件名>_`（插件）。

## 7. Schema 版本

`AlgorithmDiagnostics::diag_schema_version`（当前 1）。字段名一旦发布即视为稳定；新增字段只追加、不改名。工具可依据版本号做兼容解析。

## 8. 模板：PartitionDpDiagnostics

Catalan/分治 DP 算法的标准诊断模板（`src/domain/algorithm/diagnostics/AlgorithmDiagnostics.h` 已实现）。插件 DP 算法直接照抄此结构，改写字段与 flush 即可：

```cpp
/// DP 类算法（bb_dp / dp_merge / 插件 DP）的诊断模板。
/// Tier 0 字段在 pass 结束时由算法派生（扫描 memo cache，零计数器）；
/// Tier 1 字段在状态内本地累加、状态结束 flush 一次（≤2^n 次原子）。
struct PartitionDpDiagnostics : SearchDiagnostics {
    uint64_t dp_subproblems_solved{0};    // Tier 0: 实际计算过的子问题数（memo 未命中）
    uint64_t dp_cache_slots{0};           // Tier 0: memo 槽位总数（flat: 1<<n）
    uint64_t dp_cache_hits{0};            // Tier 0: 命中数（= slots - solved，flat 路径）
    uint32_t dp_max_frontier_size{0};     // Tier 0: 任意子问题 frontier 最大大小
    uint64_t dp_cap_pruned{0};            // Tier 1: 被 max_step_cost 剪掉的 forge 数
    uint64_t dp_bound_pruned{0};          // Tier 1: 被 B&B bound 剪掉的 combine 数
    uint64_t dp_pareto_dropped{0};        // Tier 1: 被 Pareto 支配剪掉的条目数
    int32_t  dp_ub_cost{INT32_MAX};       // Tier 0: 初始上界成本（compute_ub 结果）
    bool     dp_pass_b_ran{false};        // Tier 0: 是否运行了无约束 Pass B

    void flush(std::vector<DiagnosticsWriter::Entry>& out) const override {
        SearchDiagnostics::flush(out);    // 必须先调父类
        out.push_back({"dp_subproblems_solved", static_cast<int64_t>(dp_subproblems_solved)});
        out.push_back({"dp_cache_slots",        static_cast<int64_t>(dp_cache_slots)});
        out.push_back({"dp_cache_hits",         static_cast<int64_t>(dp_cache_hits)});
        out.push_back({"dp_max_frontier_size",  static_cast<int64_t>(dp_max_frontier_size)});
        out.push_back({"dp_cap_pruned",         static_cast<int64_t>(dp_cap_pruned)});
        out.push_back({"dp_bound_pruned",       static_cast<int64_t>(dp_bound_pruned)});
        out.push_back({"dp_pareto_dropped",     static_cast<int64_t>(dp_pareto_dropped)});
        out.push_back({"dp_ub_cost",            static_cast<int64_t>(dp_ub_cost)});
        out.push_back({"dp_pass_b_ran",         dp_pass_b_ran ? 1 : 0});
    }
};
```

**配套实现模式（在算法内）**：

```cpp
// 1) 算法持有聚合原子（pass 生命周期），结束写入 _diag
std::atomic<uint64_t> _dp_pareto_dropped{0};

// 2) cache_put 时把该子问题 frontier 的本地 drop 数并入全局
_dp_pareto_dropped.fetch_add(static_cast<uint64_t>(frontier.dropped),
                             std::memory_order_relaxed);

// 3) Tier 0：pass 结束扫描 flat cache（一次性，零热路径开销）
uint64_t solved = 0, max_f = 0;
for (size_t i = 0; i < _flat_capacity; ++i)
    if (auto* f = _flat_cache[i].load(std::memory_order_relaxed)) { ++solved; max_f = max(max_f, f->entries.size()); }
_diag.dp_subproblems_solved = solved;
_diag.dp_cache_slots        = _flat_capacity;
_diag.dp_cache_hits        = _flat_capacity - solved;   // flat 路径
_diag.dp_max_frontier_size = static_cast<uint32_t>(max_f);
```

## 9. 插件落地指引（step-by-step）

给插件作者（如 `idastar` 的 `IDAStarDiagnostics` 是既有范本）：

1. **选基类**：按 §3 分类学选 `AlgorithmDiagnostics` / `SearchDiagnostics` / `PoolSearchDiagnostics` / `PartitionDpDiagnostics`。
2. **建派生结构**：放在插件自己的 `.h`，声明 `struct MyAlgoDiagnostics : <基类> { ... }`。
3. **写 `flush()`**：先 `基类::flush(out)`，再 `out.push_back({...})` 自己的字段（命名带插件前缀）。
4. **算法内持有**：成员 `MyAlgoDiagnostics _diag;`，在 `execute()` 填充后 `ctx.set_exit_diagnostics(_diag)`。
5. **Tier 0/1 填充**：派生字段 pass 结束一次写；热路径计数用 §5 的聚合模式。
6. **禁 Tier 2 无条件使用**：若确实需要每操作计数，放 `#ifdef BESQ_DEEP_DIAGNOSTICS` 后。
7. **头文件文档块**：说明范式、复杂度、以及"这个诊断能揭示什么（valgrind 盲区）"。

## 10. 参考：valgrind 实验数据

- cachegrind（dp_merge, sword_12, 优化前）：`solve` 431M+48M Ir、`forge_into` 296M、malloc/free ≈ 190M+62M。**但无法区分 cache 命中 vs 重算，无法归因剪枝。**
- massif（sword_16）：峰值 164.8MB，内存非瓶颈。
- E1 实验（bb_dp, sword_16）：`incr_steps_forged()` 的 relaxed 原子在 32 线程下因 cacheline 争抢产生 ~203M 次 RMW ≈ **1.5s**。→ 直接促成 §5 的三层规范。

---

```
docs/algotithm_designs/algorithm-diagnostics-spec.md — 2026-08-02
```
