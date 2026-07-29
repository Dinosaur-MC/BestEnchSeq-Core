# 内置算法策略（`src/domain/algorithm/_strategies/`）

本目录存放编译进 `besq-core` 的内置策略。外部策略作为插件独立构建，见 `plugins/`。

## 策略模式

每个算法策略实现 `IAlgorithm` 纯虚接口，通过 `AlgorithmRegistry` 工厂注册。策略层**只能用 compact 类型**，零 domain 依赖。

```cpp
class IAlgorithm {
public:
    virtual std::string_view name() const noexcept = 0;
    virtual void execute(const AlgorithmInput &input, ExecutionContext& ctx) = 0;
};
```

## 内置策略列表

### 确定性合成算法

| 策略 | 目录 | 复杂度 | 方法 |
|---|---|---|---|
| Hamming | `hamming/` | O(n log n) | Popcount 平衡二叉合并树 |
| dp_merge | `dp_merge/` | O(2^N) 带剪枝 | 分治 DP + (EnchSet, PPN) Pareto 分桶 |

确定性算法**不展开搜索树**，通过固定策略合并物品。速度快但解的质量不可控。

dp_merge 是最快的确定性算法，对于 N ≤ 10 可在毫秒级达到最优解（见对比数据）。

### 搜索算法

| 策略 | 目录 | 方法 | 状态表示 |
|---|---|---|---|
| DFS | `dfs/` | 回溯 + 哈希剪枝 + 启发式剪枝 | `vector<Item>` 拷贝，哈希化 visited 表 |
| A* | `astar/` | 最佳优先 + ItemPool ID 索引 | `vector<ItemID>` + best_g 表 |

搜索算法可以找到更优解，但时间随搜索空间指数增长。

> 更多策略（Greedy、DiffFirst、HierarchicalMerge、DynamicPenaltyBalance、IDA*）以插件形式提供，见 `plugins/`。

## 注册机制

内置策略由 CMake 自动发现并注册，无需手动维护列表：

1. CMake globs `_strategies/*/*Algorithm.h`
2. 生成 `_strategy_registration.cpp`，包含所有策略头文件并调用 `reg.register_algorithm(name, factory)`
3. 生成的 `.cpp` 编译为 `besq-core` 的一部分
4. `AlgorithmLoader::load_builtin()` 调用 `besq_register_builtin_strategies()` 完成注册

新增内置策略只需在 `_strategies/<name>/` 下放入文件，CMake 下次 configure 时自动包含。

## 诊断类型选择

| _diag 类型 | 适用策略 |
|---|---|
| `AlgorithmDiagnostics` | 确定性算法（Hamming、dp_merge）|
| `SearchDiagnostics` | 搜索但无 ItemPool（DFS）|
| `PoolSearchDiagnostics` + 具体类型 | 有 ItemPool 的搜索（A* → `AStarDiagnostics`）|

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
  └─ 搜索策略（A*/DFS）：
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
- **启发式 buffer**：成员变量 `_h_buf` / `_h_dirty` 在 `execute()` 中复用，不要每次展开时堆分配
- **`ForgeEngine` 引用**：传 `const&` 或值拷贝到 lambda，不要捕获 `this` 然后用 `_forge_engine`

### 通用

- **解方案去重**：`ExecutionContext::append_solution()` 已在 `report_solution()` 内部自动调用，算法不要重复添加
- **early return**：取消或超时后应尽快 return，不要在返回后继续展开
- **`_diag` 字段**：只填算法相关的字段，框架会补充 `algorithm_name`、`wall_ms`、原子计数器

## 新增内置策略开发清单

- [ ] 在 `_strategies/<name>/` 下创建 `NameAlgorithm.h` 和 `NameAlgorithm.cpp`
- [ ] 类名与文件名匹配：`<name>/NameAlgorithm.h` → `algorithm::NameAlgorithm`
- [ ] 继承 `IAlgorithm`，实现 `name()` / `version()` / `execute()`
- [ ] 选择正确的 `_diag` 类型（见上表）
- [ ] 实现 `execute()`，按规范使用 ExecutionContext API
- [ ] 目录名成为注册名（CMake 自动 glob `*Algorithm.h`，生成注册代码）
- [ ] 如果使用 ItemPool，添加 `simulate()` 快速可行性检查
- [ ] 在 `tests/domain/algorithm/test_algorithm_strategies.cpp` 中添加测试
- [ ] 运行 `cmake --build build` 让 CMake 重新 glob 发现新策略
- [ ] 运行 `ctest` 确认全部测试通过
