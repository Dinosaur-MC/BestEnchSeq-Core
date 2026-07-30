# 算法层（`src/domain/algorithm/`）

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│  IAlgorithm  ── 纯虚策略接口                                │
│     ├── Hamming             Popcount 平衡树     [内置]       │
│     ├── DFS                 DFS + 哈希剪枝      [内置]       │
│     ├── AStar               A* 搜索（ItemPool） [内置]       │
│     ├── dp_merge            分治 DP + Pareto    [内置]       │
│     ├── Greedy              贪心合并            [插件]       │
│     ├── DiffFirst           PPN 分层合并        [插件]       │
│     ├── HierarchicalMerge   分层合并            [插件]       │
│     ├── DynamicPenalty      动态惩罚均衡        [插件]       │
│     └── IDAStar             IDA* 搜索（TT）     [插件]       │
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
| `Ench` (`Enchantment`) | 2 B | `uint8_t id` + `uint8_t level` |
| `EnchSet` | 72 B inline | `uint64_t` 位掩码 + `uint8_t[64]` 等级数组，最多 64 附魔 |
| `EnchInfo` | 12 B | `uint8_t id/mul/mul_b/max_lvl + mask_type exc_mask + bool` |
| `EnchReg` | ~4.5 KB | 固定 `array[64][64]` 冲突矩阵 + `array[64]` 掩码缓存 |
| `Item` | ~80 B | type + dur + ppn + EnchSet |
| `Equipment` | — | id + max_durability + `unordered_set<uint8_t>` 适用附魔 |
| `EnchSolution` | — | 步骤序列 + 总消耗 |

### EnchSet 访问规范

**优先使用非迭代器 API**（更快、更简洁）：

```cpp
// ✅ 推荐：非迭代器访问
ench_set.contains(id);          // O(1) 判断是否存在
ench_set[id];                   // O(1) 获取等级
ench_set.first();               // 第一个附魔 ID，无则 EnchSet::npos
ench_set.next(id);              // 下一个附魔 ID，无则 npos
ench_set.next_level(id);        // 下一个附魔的等级

// ✅ 位运算（EnchSet::mask_type = uint64_t）
auto same = target & sacrifice;  // 交集掩码
auto diff = sacrifice - target;  // 差集掩码（sacrifice 独有）

// ✅ 位遍历（sbit_iterator / bit_iterator）
sbit_iterator<mask_type, uint8_t> it(mask);
for (; it; ++it) {               // 满足 bool 检查
    ench_set[*it];               // *it 是附魔 ID
}
// 或 next() 风格：
bit_iterator<mask_type, uint8_t> it(mask);
for (auto i = it.next(); i != it.npos; i = it.next()) {
    ench_set[i];                 // i 是附魔 ID
}

// ❌ 避免：迭代器风格（除非需要同时访问 id 和 level）
for (const auto &e : ench_set) {
    e.id();        // 方法调用（代理迭代器）
    e.level();
}
```

参见 `src/common/utils/bit_iterator.hpp` 获取 `bit_iterator` / `sbit_iterator` 的完整文档。

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
