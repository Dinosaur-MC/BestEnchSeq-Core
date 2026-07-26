# 算法层（`src/domain/algorithm/`）

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│  IAlgorithm  ── 纯虚策略接口                                │
│     ├── Hamming             Popcount 平衡树   [内置]        │
│     ├── DFS                 DFS + 剪枝         [内置]        │
│     ├── AStar               A* 搜索（ItemPool）[内置]        │
│     ├── Greedy              贪心合并           [插件]        │
│     ├── DiffFirst           PPN 分层合并       [插件]        │
│     ├── HierarchicalMerge   分层合并           [插件]        │
│     ├── DynamicPenalty      动态惩罚均衡       [插件]        │
│     └── IDAStar             IDA* 搜索（TT）    [插件]        │
│                                                             │
│  AlgorithmExecutor  ─── 异步执行引擎                         │
│     ├── 状态机: Idle→Running→Paused→Completed|Failed|Cancelled
│     ├── 工作线程管理 + 热身                                 │
│     └── _finalize() 收集诊断 → Exit 事件                    │
│                                                             │
│  ExecutionContext  ─── 一站式算法交互接口                   │
│     ├── 执行控制: cancel/pause/resume                       │
│     ├── 原子计数器: incr_nodes_visited/pruned/forged       │
│     ├── 流式通知: report_progress / report_solution         │
│     ├── 退出诊断: set_exit_diagnostics                      │
│     └── 解法累积: get_solutions                             │
│                                                             │
│  ForgeEngine ─── 锻造引擎（forge_into / forge / etc.）     │
│                                                             │
│  DiagnosticsService ─── 全局诊断管道（异步持久化 + Observer）│
└─────────────────────────────────────────────────────────────┘
```

## 数据管道

```
CLI → EnchParser/ItemParser (registry-aware)
  → SolvePipeline::stage_apply()
  → CompactAdapter::apply()
  → AlgorithmInput (compact)                     ← 分界线
      │
      ▼
  AlgorithmExecutor::start(input)
  → IAlgorithm::execute(input, ctx)             ← 算法全用 compact 类型
      │
      │  ├─ ctx.incr_nodes_visited()            热路径
      │  ├─ ctx.report_progress(pct, status)    流式
      │  ├─ ctx.report_solution(steps)          流式 + 累积
      │  └─ ctx.set_exit_diagnostics(...)       退出
      │
      ▼
  AlgorithmExecutor::output()
  → AlgorithmOutput (compact)                    ← 分界线
      │
      ▼
  CompactAdapter::recall()
  → EnchSolutions (domain)
  → OutputFormatter::format_*()
```

## 紧凑类型约定

算法层**只能使用 `algorithm` 命名空间中的类型**，不允许 domain 类型泄漏进来。核心紧凑类型定义在 `src/domain/algorithm/types/` 下。

核心紧凑类型：

| 类型 | 内存 | 说明 |
|---|---|---|
| `Enchantment` (Ench) | 4 B | int16_t id + level |
| `EnchSet` | ≤ 64 B inline | 最多 16 附魔，零堆分配 |
| `Item` | ~72 B | type + dur + ppn + EnchSet |
| `Equipment` | 8 B | type + max_durability + applicable_tag_mask |
| `EnchSolution` | — | 步骤序列 + 总消耗 |

## 开发规范

### 新增一个内置算法的步骤

1. 在 `_strategies/<name>/` 下创建目录，实现 `IAlgorithm`
2. 选择合适的 `_diag` 类型：
   - 确定性策略 → `AlgorithmDiagnostics`
   - 搜索策略（有展开节点） → `SearchDiagnostics`
   - 池化搜索（A*/IDA*） → `PoolSearchDiagnostics` + 对应具体类型
3. 实现 `execute(input, ctx)`，使用以下 API 与框架交互：

```cpp
// 热路径计数器（每次展开/剪枝/锻造调用）
ctx.incr_nodes_visited();
ctx.incr_nodes_pruned();
ctx.incr_steps_forged();

// 流式通知（内部直调 DiagnosticsService::push）
ctx.report_progress(pct, status);         // uint8_t 0-100, 5% 限频
ctx.report_solution(steps);               // 自动推送到 observer + 累积

// 退出诊断（填充 _diag 字段后移交所有权）
_diag.status = "Complete";
_diag.solution_cost = cost;
ctx.set_exit_diagnostics(_diag);          // 模板推导具体类型
```

4. 在 `_strategies/Registration.h` 中使用 `BESQ_REGISTER_STRATEGY` 宏注册工厂
5. 添加单元测试到 `tests/domain/algorithm/`

> 注意：内置算法由 CMake 自动发现，无需修改 `main.cpp`。CMake globs `_strategies/*/*Algorithm.h`，
> 生成 `_strategy_registration.cpp`，编译为 `besq-core` 的一部分。
> `AlgorithmLoader::load_builtin()` 调用 `besq_register_builtin_strategies()` 完成注册。

### 性能注意事项

- `incr_*` 系列使用 `memory_order_relaxed`，零同步开销
- `report_progress` 内部 5% 限频，高频调用不会产生队列拥塞
- `report_solution` 提供 `const&`（1 次 copy）和 `&&`（0 copy）两个重载
- `set_exit_diagnostics` 使用模板推导，不需要在算法中写具体类型名
- 所有 `to_string` 只在 `DiagnosticsService` 的 EventLoop 后台线程发生，算法线程零开销
