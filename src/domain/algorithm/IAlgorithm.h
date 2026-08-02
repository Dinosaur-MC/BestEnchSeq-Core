#pragma once
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "forge_engine/IForgeEngine.h"
#include <memory>

namespace algorithm {
class ExecutionContext;
class IAlgorithmSerializer;
class IResolver;

// ─── IAlgorithm (pure interface, compact-only) ───
//
// == 算法策略设计核心规范 =================================================
//
// 所有内置策略和插件策略必须遵守以下规范。违反规范的策略可能在代码审查
// 中被拒绝，或在运行时出现不可预期的行为。
//
// 诊断与性能规范的完整版见 docs/algotithm_designs/algorithm-diagnostics-spec.md。
//
// --- 1. 内存分配规则 ---------------------------------------------------
//
//   构造期不允许堆分配  所有预分配必须推迟到 execute() 中。
//                     构造函数应 noexcept 且零分配。
//                     反例：IDASTAR 的 TTTable 构造期分配 25MB
//                     已被修复为首次 store() 延迟分配。
//
//   execute() 内分配   scratch buffer 应在 execute() 开始时 resize/assign，
//                      不要在状态展开循环中反复分配。
//                      反例：_target_level_map.assign(reg.size(), 0) ✅
//
//   ItemPool 预分配      如果使用 ItemPool，在 execute() 中 reserve(est)。
//                      估计值 = min(factorial_estimate, max_pool_cap)。
//
// --- 2. 热路径计数器（已修订，完整规范见算法诊断规范 §5）--------------
//
//   ⚠ 已废弃"每操作必调计数器"的强制要求。relaxed 原子没有 fence，但
//   有 cacheline 争抢：每 forge 一次的 incr_steps_forged() 在 sword_16 上
//   实测 203M 次 ≈ 1.5s（见 issues/bbdp-turnaround.md E1）。
//
//   正确做法 —— 按性能分层（docs/.../algorithm-diagnostics-spec.md §5）：
//     Tier 0  零成本：pass 结束时派生指标（扫描 memo cache 等），无条件用。
//     Tier 1  可忽略：每状态/子问题一次原子（≤ 2^n），可无条件用。
//     Tier 2  每操作计数器（ExecutionContext::incr_*）：禁止无条件使用；
//             必须门控在 BESQ_DEEP_DIAGNOSTICS 后（默认关，空内联函数 =
//             调用点整体消除，真正零成本）。
//
// --- 3. 流式通知 -------------------------------------------------------
//
//   report_progress(pct, status)   5% 限频，observer 异步接收。
//   report_solution(steps)         自动推送到 observer + 累积到 output。
//                                  提供 const&（1 次 copy）和 &&（0 copy）重载。
//
// --- 4. 退出诊断（完整规范见 docs/.../algorithm-diagnostics-spec.md）-----
//
//   填充策略相关的 _diag 字段后调用 ctx.set_exit_diagnostics(_diag)。
//   框架自动补充 algorithm_name、wall_ms。
//   _diag 类型选择（按搜索范式，见规范 §3）：
//     AlgorithmDiagnostics      确定性合成算法（Hamming、diff_first）
//     SearchDiagnostics         展开式搜索，无 ItemPool（DFS——插件）
//     PoolSearchDiagnostics     有 ItemPool 的搜索（A*、IDA*——插件）
//     PartitionDpDiagnostics    Catalan/分治 DP（bb_dp、dp_merge）
//   公共核心必填（规范 §4）：status / solution_cost / normalized_explored_states。
//   字段命名带范式前缀（dp_ / search_ / 插件名_），见规范 §6。
//
// --- 5. 编译期检查 -----------------------------------------------------
//
//   所有策略必须添加以下 static_assert：
//     - 非拷贝非平凡：   static_assert(!std::is_trivially_copyable_v<MyAlgo>);
//     - 默认 noexcept：  static_assert(std::is_nothrow_default_constructible_v<MyAlgo>);
//     反例：TTTable 的 BUCKETS 2 的幂检查和 PEAK_BYTES < 256 MiB 检查。
//
// --- 6. 注册方式 -------------------------------------------------------
//
//   内置策略：在 _strategies/<name>/ 下放置 *Algorithm.h 和 *.cpp。
//            CMake 自动 glob *Algorithm.h，生成注册代码。
//            目录名 <name> 成为注册名。
//            无需手动修改 Registration.h 或 CMakeLists.txt。
//
//   插件策略：在 plugins/<name>/ 下构建独立共享库。
//            提供 besq_create_algorithm() 入口。
//
// --- 7. 文档要求 -------------------------------------------------------
//
//   头文件顶部必须包含文档注释块，说明：
//     - 算法核心思路和复杂度
//     - 预期用例和推荐的使用场景
//     - 如果有理论保证（最优性、完备性），明确注明
//     反例：HammingAlgorithm.h 的 popcount 平衡树文档 ✅
//          DPMergeAlgorithm.h 的分治 DP + Pareto 分桶文档 ✅
//
// =========================================================================

class IAlgorithm {
  public:
    virtual ~IAlgorithm() noexcept = default;

    virtual std::string_view name() const noexcept    = 0;
    virtual std::string_view version() const noexcept = 0;
    /// Returns the set of operation modes this algorithm supports.
    /// Default: direct mode only.
    virtual AlgorithmMode supported_mode() const noexcept { return AlgorithmMode::direct; }
    /// Whether this algorithm supports checkpoint serialization for resume.
    /// Default returns false.  Resumable algorithms must override to return true.
    virtual bool is_resumable() const noexcept { return false; }
    /// Predicted wall-clock time to solve an input with \p ench_count target
    /// enchantments, in **seconds**.  Deterministic (greedy/constructive)
    /// algorithms return 0.  Used by tooling (e.g. the benchmark's dynamic
    /// tier matrix) to skip algorithms whose predicted runtime exceeds the
    /// configured budget.  Fitted from the scaling benchmark (Release);
    /// fits are per-family exponentials with a ~30% safety margin.
    virtual double evaluate(int16_t ench_count) const noexcept = 0;

    /// Initialize before execute().  Called once by AlgorithmExecutor before
    /// each execute() call.  Algorithms that support checkpoint resumption
    /// should check ctx.is_restored() here to distinguish fresh starts from
    /// restored state and skip redundant pre-allocation.
    virtual void init(const AlgorithmInput &input, const ExecutionContext &ctx) {
        (void)input; (void)ctx;
    }

    /// Execute the algorithm on the given input (takes ownership by value to
    /// prevent external modification during long-running search).
    virtual void execute(const AlgorithmInput &input, ExecutionContext &ctx) = 0;

    /// Quick feasibility check: returns true if the target is reachable
    /// from the given input without computing exact costs.
    /// Default: pessimistic but catches trivial cases (empty pool, target
    /// already met, no books to work with).  Strategies may override for
    /// tighter checks.  Must agree with DefaultResolver's reachability rules.
    virtual bool simulate(const AlgorithmInput &input) const noexcept;

    /// Returns the associated forge engine for this algorithm.
    virtual std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept = 0;

    /// Returns the resolver that turns an AlgorithmInput into the strategy's
    /// working item set.  Default: DefaultResolver.  Strategies call this
    /// inside execute()/simulate().
    virtual std::unique_ptr<IResolver> get_resolver() const noexcept;

    /// Returns the associated serializer for this algorithm's state.
    /// Returns nullptr if the algorithm does not support serialization.
    virtual IAlgorithmSerializer *get_serializer() noexcept { return nullptr; }
    virtual const IAlgorithmSerializer *get_serializer() const noexcept { return nullptr; }
};

} // namespace algorithm
