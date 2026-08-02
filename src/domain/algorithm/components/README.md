# 共享组件（`src/domain/algorithm/components/`）

通用搜索基础设施，被多个算法策略共享。策略专用组件（如 `AStarMemoryBudget` → `strategies/astar/`、`TTTable` → `strategies/idastar/`）已移至对应策略目录。

## 组件一览

### ItemPool

写后哈希去重的物品池，`pmr` 单调分配器。

```
add(Item) → ItemID       // 哈希去重（64-bit），碰撞罕见时直接存储
operator[](ItemID) → Item // O(1) 索引
```

- 所有 Item 连续存储，`ItemID = int32_t` 直接索引
- 默认容量上限 10M，超限返回 `INVALID_ITEM_ID`
- A* 用 ItemPool 为搜索状态提供稳定索引，避免 `vector<Item>` 拷贝
- `hash_ids(ids)` — ID 序列经池解析后的内容哈希（visited/转置表键），复用 `std::hash<Item>`

### SearchUtils

搜索工具函数，被 Heuristic / HeuristicBasic 和所有搜索算法直接使用。

- `fill_max_levels(range, reg, buf, dirty)` — 从物品集合收集每个附魔的最高等级
- `compute_h(target, reg, filled_buf)` — 根据目标附魔计算启发式下限

复杂度 O(n×m)，其中 n = 物品数，m = target 附魔数。

### Heuristic / HeuristicBasic

可采纳启发函数，返回剩余成本的**下限**。

**Heuristic**（A* / IDA* 使用）：对每个目标附魔缺失等级 × 书本乘数求和，忽略惩罚和冲突。

**HeuristicBasic**（DFS 使用）：简化版，与 Heuristic 算法一致但接口不同。

```
compute(ids, pool, reg, target, buf, dirty) → h
// h ≤ 实际剩余成本（可采纳性保证）
```

调用方提供 scratch buffer (`buf` / `dirty`) 以避免每次调用的堆分配。Heuristic 在展开数百万次时复用同一组 buffer。

## 使用规范

- **ItemPool** 不保证线程安全（每个搜索实例独占）
- **SearchUtils** 纯函数，无状态
- **Heuristic** buffer 应复用搜索实例级别的 `_h_buf` / `_h_dirty`，不要每次都重新申请
- `fill_max_levels` 的 yield lambda 在每个物品的每个附魔上调用，保持轻量

### EnchSet / bit_iterator

算法开发中：

- **优先使用非迭代器 API**：`contains(id)`、`operator[](id)`、`first()`、`next()`
- **集合运算**：`operator&` / `operator-` 返回 `uint64_t` 掩码，配合 `bit_iterator` 或 `sbit_iterator` 遍历
- **bit_iterator**（`src/common/utils/bit_iterator.hpp`）：低开销位扫描，适用于遍历 EnchSet 差集/交集
- **sbit_iterator**：迭代器风格的 `operator*` / `operator++` / `operator bool`，适合 `for (it; it; ++it)` 模式
- **哨兵值**：`bit_iterator::npos` 和 `sbit_iterator::npos` 表示遍历结束

## 后续规划

- 通用 `TTTable`（当前 IDAStar 专用版在 `plugins/idastar/`，计划抽离为公共组件）
