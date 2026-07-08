# 最优锻造序列搜索：算法探讨

> 本文探讨 BestEnchSeq-Core 核心问题的算法设计：给定一组初始物品和期望目标，
> 如何找到经验消耗最少的铁砧操作序列。

---

## 1. 问题定义

### 形式化描述

**输入：**
- 初始物品集合 $\{S_1, S_2, \dots, S_k\}$，其中 $S_0$ 为初始目标物品槽
- 期望目标状态 $S_{\text{target}}$（物品类型、附魔集合、耐久度要求）

**输出：**
- 一棵合成树：内部节点为铁砧操作，叶节点为初始物品，根节点为最终成品
- 每步操作的等级消耗及总消耗
- 目标：最小化总经验消耗

### 约束

- 每次操作消耗两个物品、产生一个新物品
- 每个物品只能使用一次
- 总操作步骤无上限（但受惩罚值指数增长的约束）
- 复杂度：$k$ 个初始物品，穷举搜索空间为 $\Omega(k!)$

### 与标准问题的关系

最优锻造序列问题可归约为**最优二叉树**（Optimal Binary Tree）的变体：

- 叶节点 = 初始物品，有类型、附魔、惩罚属性
- 内部节点 = 铁砧操作，成本由左右子节点属性决定
- 每个节点的成本影响其父节点的成本（通过惩罚值传递）
- 子树顺序可交换（但成本不对称，$A+B \neq B+A$）

---

## 2. 搜索空间分析

### 组合爆炸

以 $n$ 本附魔书 + 1 个装备为例：

| 物品数 | 可能二叉树数 | 搜索特性 |
|--------|-------------|---------|
| 3 | 3 | 完全可穷举 |
| 4 | 15 | 可穷举 |
| 5 | 105 | 已需剪枝 |
| 6 | 945 | 简单剪枝可解 |
| 8 | 135,135 | 需强力剪枝 |
| 10 | 34,459,425 | A*/IDA* 级别 |
| 14 | $> 10^{11}$ | 需启发式近似 |

> 二叉树数量 = $(2n-3)!!$，考虑左右交换后乘 $2^{n-1}$。

### 惩罚值指数增长

惩罚值 $P = 2^n - 1$ 的增长特性决定了搜索树深度受限：

| 操作次数 | 惩罚值 | 累积(若每次+39) |
|---------|-------|----------------|
| 0 | 0 | 0 |
| 1 | 1 | 1 |
| 2 | 3 | 4 |
| 3 | 7 | 11 |
| 4 | 15 | 26 |
| 5 | 31 | 57（>> 39） |
| 6 | 63 | 120 |

**关键推论：** 任意物品的使用次数（作为中间节点深度）不应超过 5 次。锻造序列的深度存在自然上限，这显著限制了搜索空间。

---

## 3. 算法策略对比

### 3.1 贪心算法（Greedy）

**策略：** 每步选择当前代价最低的合并操作。

```cpp
// 当前实现：GreedyAlgorithm
current = target_item;
for each sacrifice in available_items {
    cost = forge_into(current, sacrifice);
    record_step(current, sacrifice, cost);
}
```

| 方面 | 评价 |
|------|------|
| 时间复杂度 | $O(k)$ — 最快 |
| 最优性 | 不保证 |
| 适用场景 | 在线计算，或作为上界用于剪枝 |
| 当前状态 | 已实现 |

**局限**：贪心无法感知全局惩罚增长。例如，将高惩罚书留到最后合并可能优于立即合并。

### 3.2 深度优先搜索 + 分支定界（DFS+B&B）

**策略：** 系统地枚举所有可能的合并顺序，用当前最优解剪枝。

```
dfs(state, cost_so_far):
    if cost_so_far >= best_known: return        // 剪枝
    if state matches target: update_best(); return
    for each pair (i, j) in available_items:
        new_item = forge(items[i], items[j])
        new_state = state.replace(i,j, new_item)
        dfs(new_state, cost_so_far + cost(i,j))
```

| 方面 | 评价 |
|------|------|
| 时间复杂度 | $O(k!!)$ — 指数，但剪枝有效 |
| 最优性 | 保证最优（给定可采下界） |
| 空间复杂度 | $O(k^2)$ — 递归深度 |
| 适用场景 | $k \le 8$ 时实用 |

**剪枝下界函数：**

$$h(\text{state}) = \sum_{e \in missing} \min\_\text{cost}(e, \text{source})$$

其中 $\min\_\text{cost}(e, \text{source})$ 是获得附魔 $e$ 的最低可能花费（忽略惩罚和冲突）。由于忽略惩罚下界 ≤ 实际成本，此下界是可采的。

### 3.3 A* 搜索

**策略：** 用优先队列维护搜索边界，按 $f = g + h$ 展开最优节点。

```
astar(initial, target):
    open = priority_queue ordered by f = g + h
    closed = set of visited states (hashed by item multiset signature)
    open.push(initial, h(initial))
    while open not empty:
        current = open.pop()
        if current matches target: return reconstruct_path(current)
        if current in closed: continue
        closed.insert(current)
        for each pair (i, j):
            next = forge(current, i, j)
            if next not in closed or g[next] > g[current] + cost:
                g[next] = g[current] + cost
                open.push(next, g[next] + h(next))
```

| 方面 | 评价 |
|------|------|
| 时间复杂度 | 最坏仍指数，但通常优于 DFS+B&B |
| 最优性 | 保证最优（可采启发） |
| 空间复杂度 | $O(\text{状态数})$ — 需存储 open/closed |
| 适用场景 | $k \le 10$，需要严格最优 |

**状态散列**：将当前物品集合编码为规范形式的哈希（按附魔组合排序），以检测重复状态。两个不同顺序生成相同物品集合时合并为一个状态。

### 3.4 动态规划（DP）

**策略：** 将子问题定义为「从物品子集 $M$ 产生物品 $X$ 的最小成本」。

$$dp[M][X] = \min_{(i,j) \in M, \text{forge}(i,j)=X} \left( \min_{A \subset M \setminus \{X\}} dp[A][i] + dp[M \setminus A][j] + \text{cost}(i,j) \right)$$

| 方面 | 评价 |
|------|------|
| 时间复杂度 | $O(3^k)$ 或 $O(2^k \cdot k^2)$ |
| 最优性 | 保证最优 |
| 空间复杂度 | $O(2^k \cdot m)$，$m$ 为中间物品种类 |
| 适用场景 | $k \le 6$（状态空间爆炸快） |

**挑战**：中间物品种类 $m$ 随 $k$ 指数增长，状态可用性受限于 $k \le 6$。

### 3.5 算法选择指南

| 场景 | 推荐算法 | 原因 |
|------|---------|------|
| 快速估算、初始解 | 贪心 | O(k)，免搜索 |
| 少量物品 ($k \le 6$) | DP 或 A* | 可保证最优 |
| 中等规模 ($k \le 10$) | A* / IDA* | 空间-时间平衡好 |
| 大规模 ($k > 10$) | 贪心 + 局部搜索 | 启发式近似 |
| 需要流式输出 | DFS+B&B | 逐步输出次优解 |

---

## 4. 启发函数设计

### 4.1 可采启发（用于 A*/分支定界）

$$h_{\text{admissible}}(S) = \sum_{e \in \text{target} \setminus S} \text{level}(e) \times \text{book\_multiplier}(e)$$

- 忽略惩罚（假设为零）
- 忽略冲突（假设所有附魔兼容）
- 使用最低乘数（书本乘数 ≤ 装备乘数）
- 因此 $h \le \text{实际剩余成本}$，保证可采

### 4.2 增强启发（更紧的下界）

$$h_{\text{enhanced}}(S) = \sum_{e} \text{cost}(e) + \min\_\text{penalty\_increase}$$

- 考虑至少一次惩罚增加（合并两个物品后，结果至少 $P=1$）
- 考虑已知冲突的成本
- 更紧的下界 → 更有效的剪枝

### 4.3 不可采启发（用于近似解）

$$h_{\text{greedy}}(S) = \text{greedy\_solve}(S)$$

- 用贪心算法快速求解剩余问题
- 不可采（可能高估），但剪枝极其有效
- 用于快速找到高质量解而非最优解

---

## 5. 剪枝策略

### 5.1 安全剪枝（不丢失最优解）

| 策略 | 原理 |
|------|------|
| 惩罚下界剪枝 | $g + h_{\text{admissible}} \ge \text{best}$ → 剪枝 |
| 对称性剪枝 | 交换 $A+B$ 和 $B+A$ 方向只搜一次 |
| 单调性剪枝 | 同一子集已生成过的中间结果不重复搜索 |

### 5.2 启发式剪枝（可能丢失最优解）

| 策略 | 原理 | 风险 |
|------|------|------|
| 深度限制 | 合并深度 > 5 时放弃（惩罚已过大） | 极少数边缘最优解可能被剪 |
| 成本阈值 | 单步成本 > 39 时放弃 | 安全（游戏本身不允许） |
| 贪心上界 | 先跑贪心得到 `best`，再跑精确搜索 | 不会丢失比贪心更好的解 |

### 5.3 状态去重

附魔书 $(A,B,C)$ 以不同顺序合并时，可能产生相同中间结果。通过将物品集合按规范形式（附魔组合排序、惩罚值匹配）存储到哈希表，可以避免重复探索：

```
# 去重命中示例
A+B → AB, 然后 AB+C → ABC  vs  A+C → AC, 然后 AC+B → ABC
// 如果 AB 和 AC 都导致相同的 ABC 状态（惩罚值相同），只保留最小成本路径
```

---

## 6. 实现架构

### 执行引擎集成

```
AlgorithmExecutor
  └─ DFSAlgorithm / AStarAlgorithm
       ├─ 搜索循环（同步）
       ├─ 调用 IForgeEngine::forge_into()
       ├─ 调用 ExecutionContext::report_progress()
       ├─ 调用 ExecutionContext::report_solution_found()
       └─ 定期检查 ExecutionContext::is_cancelled() / wait_if_paused()
```

### 搜索专用优化

对于搜索算法（非贪心），以下优化在 `DefaultForgeEngine` 之上是必要的：

1. **`forge_into()` 原地操作** — 搜索树每个节点调用数百万次，必须避免 EnchSet 拷贝
2. **状态快照/回滚** — DFS 可使用 copy-on-write 或 undo 日志回溯，而非深拷贝
3. **预计算乘数** — `get_multiplier()` 在 hot path 中应缓存
4. **附乘集合位编码** — 将附魔 ID 映射到 `std::bitset` 或 `uint64_t` 位掩码，用于 O(1) 子集判断

### 并行化

```
搜索树 ← 工作窃取线程池
├─ 每个节点：评估所有可能的合并对
├─ 足够深时：将子节点分发给工作者线程
└─ 剪枝：全局 best_known 用 atomic 共享
```

注意：`ExecutionContext` 内的 Observer 锁定机制在多线程下需防止死锁。

---

## 7. 代码示例：DFS 搜索骨架

```cpp
class DFSAlgorithm : public IAlgorithm {
public:
    std::string_view name() const noexcept override { return "dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override {
        _best_cost = std::numeric_limits<int32_t>::max();
        _input = &input;

        // 物品集合初始化
        std::vector<ItemStack> items = {input.target_item};
        items.insert(items.end(), input.available_items.begin(), input.available_items.end());

        // 贪心预跑获得上界
        _best_cost = greedy_upper_bound(input, ctx);
        ctx.report_progress(0.0, "greedy bound: " + std::to_string(_best_cost));

        // DFS 搜索
        dfs(items, 0, ctx);

        ctx.report_progress(1.0, "done");
    }

private:
    void dfs(std::vector<ItemStack>& items, int32_t cost_so_far, ExecutionContext& ctx) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        // 下界剪枝
        if (cost_so_far + lower_bound(items) >= _best_cost)
                return;

        // 检查是否达到目标（遍历所有物品）
        for (const auto& item : items) {
            if (meets_target(item, _input->target_item)) {
                if (cost_so_far < _best_cost) {
                    _best_cost = cost_so_far;
                    // 记录解
                    ctx.report_solution_found(_current_steps);
                }
                return;
            }
        }

        // 尝试所有合并对
        for (size_t i = 0; i < items.size(); i++) {
            for (size_t j = 0; j < items.size(); j++) {
                if (i == j) continue;
                if (!_engine.is_forgeable(items[i], items[j])) continue;

                // 保存状态
                auto saved_i = items[i];
                auto saved_j = items[j];
                int32_t step_cost = _engine.forge_into(items[i], items[j]);

                _current_steps.push_back({saved_i, saved_j, step_cost, {}});

                // 移除牺牲物品 j，修正 i 下标
                items.erase(items.begin() + j);
                size_t adjusted_i = (j < i) ? i - 1 : i;

                ctx.report_progress(/*...*/);
                dfs(items, cost_so_far + step_cost, ctx);

                // 回溯：逆序恢复
                items.insert(items.begin() + j, saved_j);
                items[adjusted_i] = saved_i;

                if (ctx.is_cancelled()) return;
                }
        }
    }

    DefaultForgeEngine _engine;
    int32_t _best_cost;
    const AlgorithmInput* _input;
    std::vector<EnchSolution::EnchStep> _current_steps;
};
```

---

## 8. 总结与展望

### 当前状态

| 组件 | 状态 |
|------|------|
| 贪心算法 | ✅ 已实现 |
| DFS + 分支定界 | 设计中 |
| A* 搜索 | 规划中 |
| 启发函数 | 理论完备 |
| 并行化 | 架构预留 |

### 关键工程决策

1. **`forge_into()` 原地操作**（优先于 `forge()` 拷贝）— 性能关键
2. **搜索算法与执行引擎分离** — 算法负责纯搜索逻辑，Executor 负责生命周期
3. **流式输出** — 搜索过程中逐步报告解，而非等待完成
4. **可中断性** — 用户可随时取消长时间搜索

### 开放问题

- 何种规模的状态去重哈希值得引入？（空间-时间权衡）
- IDA* 是否比 A* 更适合有限内存环境？
- 对于 $k > 10$ 的场景，近似算法的误差界是多少？

---

## 参考

- `docs/anvil-mechanics-reference.md` — 铁砧机制算法参考
- `src/algorithm/strategies/GreedyAlgorithm.h/cpp` — 当前贪心实现
- `src/algorithm/DefaultForgeEngine.h/cpp` — 锻造引擎
- `src/algorithm/AlgorithmExecutor.h/cpp` — 异步执行引擎
