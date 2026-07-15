# 算法策略（`strategies/`）

## 策略模式

每个算法策略实现 `IAlgorithm` 纯虚接口，通过 `AlgorithmRegistry` 工厂注册。策略层**只能用 compact 类型**，零 domain 依赖。

```cpp
class IAlgorithm {
public:
    virtual std::string_view name() const noexcept = 0;
    virtual void execute(const AlgorithmInput& input, ExecutionContext& ctx) = 0;
};
```

## 策略分类

### 确定性合成算法

| 策略 | 目录 | 复杂度 | 方法 |
|---|---|---|---|
| Greedy | `greedy/` | O(n²) | 每次选预估成本最低的 pair |
| DiffFirst | `diff_first/` | O(n²) | PPN 分层，每层选最便宜的 |
| Hamming | `hamming/` | O(n log n) | Popcount 平衡二叉合并树 |
| HierarchicalMerge | `hierarchical/` | O(n²) | 分组合并，递归 |
| DynamicPenaltyBalance | `penalty_balance/` | O(n²) | 动态平衡惩罚成本 |

确定性算法**不展开搜索树**，通过固定策略合并物品。速度快但解的质量不可控。

### 搜索算法

| 策略 | 目录 | 方法 | 状态表示 |
|---|---|---|---|
| DFS | `dfs/` | 回溯 + visited 表 + 启发式剪枝 | `vector<Item>` 拷贝 |
| A* | `astar/` | 最佳优先 + ItemPool ID 索引 | `vector<ItemID>` + best_g 表 |
| IDA* | `idastar/` | 迭代加深 + TT best_g 剪枝 | `vector<ItemID>` + TTTable |

搜索算法可以找到更优解，但时间随搜索空间指数增长。

## 诊断类型选择

| _diag 类型 | 适用策略 |
|---|---|
| `AlgorithmDiagnostics` | 确定性算法（Greedy、DiffFirst、Hamming、Hierarchical、PenaltyBalance） |
| `SearchDiagnostics` | 搜索但无 ItemPool（DFS） |
| `PoolSearchDiagnostics` + 具体类型 | 有 ItemPool 的搜索（A* → `AStarDiagnostics`、IDA* → `IDAStarDiagnostics`） |

## 算法 API 使用规范

### 执行控制

```cpp
if (ctx.is_cancelled()) return;        // 响应取消
ctx.wait_if_paused();                  // 响应暂停（非热路径）
```

### 热路径计数器

```cpp
ctx.incr_nodes_visited();              // 每次状态展开
ctx.incr_nodes_pruned();               // 每次剪枝
ctx.incr_steps_forged();               // 每次 forge()
```

这三个计数器没有同步开销（`memory_order_relaxed`），**不能省略**。所有搜索算法必须调用 `incr_nodes_visited` 和 `incr_nodes_pruned`，所有算法必须调用 `incr_steps_forged`。

### 流式通知

```cpp
ctx.report_progress(pct, ProgressStatus::Exploring);
// uint8_t 0-100, 内部 5% 限频, observer 异步接收

ctx.report_solution(steps);
// const vector<EnchStep>& → 1 次 copy（lvalue）
// vector<EnchStep>&&       → 0 次 copy（rvalue move）
```

### 退出诊断

```cpp
// 填充 _diag 字段
_diag.status = "Complete";
_diag.solution_cost = cost;
// ... 其他策略特有字段 ...

// 移交所有权（模板自动推导具体类型，不需要写类型名）
ctx.set_exit_diagnostics(_diag);
```

## 数据流

```
IAlgorithm::execute(input, ctx)
  │
  ├─ ForgeEngine 调用（锻造操作）
  ├─ ExecutionContext 调用（计数、进度、方案、诊断）
  │
  ├─ 确定性策略：
  │   循环选择 pair → forge → 判断完成 → report_solution → set_exit_diagnostics
  │
  └─ 搜索策略（A*/IDA*/DFS）：
       ├─ 状态展开循环
       │   ├─ incr_nodes_visited
       │   ├─ 启发式剪枝 → incr_nodes_pruned
       │   ├─ forge → incr_steps_forged
       │   └─ 找到解 → report_solution
       └─ 退出 → set_exit_diagnostics
```

## 性能指南

### 搜索算法特有

- **ItemPool**：在 `execute()` 开始时 `_pool.clear()` + `_pool.reserve(est)`，预分配避免搜索中 rehash
- **TTTable**：IDA* 每次迭代前 `_tt.clear()`（epoch 递增，O(1)），不要全表 memset
- **启发式 buffer**：成员变量 `_h_buf` / `_h_dirty` 在 `execute()` 中复用，不要每次展开时堆分配
- **`ForgeEngine` 引用**：传 `const&` 或值拷贝到 lambda，不要捕获 `this` 然后用 `_forge_engine`

### 通用

- **解方案去重**：`ExecutionContext::append_solution()` 已在 `report_solution()` 内部自动调用，算法不要重复添加
- **early return**：取消或超时后应尽快 return，不要在返回后继续展开
- **`_diag` 字段**：只填算法相关的字段，框架会补充 `algorithm_name`、`wall_ms`、原子计数器

## 新增策略开发清单

- [ ] 在 `strategies/<name>/` 下创建 `NameAlgorithm.h` 和 `NameAlgorithm.cpp`
- [ ] 继承 `IAlgorithm`，实现 `name()` / `version()` / `execute()`
- [ ] 选择正确的 `_diag` 类型（见上表）
- [ ] 在 `strategies/Strategies.h` 添加 `#include` + 工厂函数
- [ ] 在 `src/main.cpp` 的 `register_builtin_algorithms()` 中注册
- [ ] 实现 `execute()`，按规范使用 ExecutionContext API
- [ ] 在 `tests/algorithm/test_algorithm_strategies.cpp` 中添加测试
- [ ] 如果使用 ItemPool，添加 `simulate()` 快速可行性检查
- [ ] 运行 `ctest` 确认 35/35 全通过
