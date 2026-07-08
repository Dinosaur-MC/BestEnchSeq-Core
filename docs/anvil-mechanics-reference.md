# 铁砧机制算法参考

> 依据 Minecraft Wiki (minecraft.wiki) 整理，对齐 Java Edition 最新版本。
> 本文件作为算法层开发的权威参考，所有 forge 逻辑应与此保持一致。

## 1. 基本概念

### 铁砧操作

铁砧可执行三类操作（可同时进行）：

1. **附魔合并**（Enchantment Combining）— 将牺牲物品的魔咒合并到目标物品
2. **更名**（Renaming）— 为物品命名，费用固定 1 级
3. **耐久修复**（Durability Repair）— 使用材料修复或双物品合并修复

### 操作前提

- 目标物品和牺牲物品必须可锻造组合（forgeable）
- 总费用 ≤ 39 级，否则提示「过于昂贵！」（Too Expensive!）
- 附魔书可同时作为目标或牺牲物品，但不可用于耐久修复
- 更名本身不会使前次工作惩罚升级

## 2. 前次工作惩罚（Prior Work Penalty）

### 惩罚计数

物品每次经历铁砧操作（更名除外），惩罚计数增加：

```
result_count = max(count_target, count_sacrifice) + 1
```

| 操作次数 | 0   | 1   | 2   | 3   | 4   | 5   | 6   |
| -------- | --- | --- | --- | --- | --- | --- | --- |
| 惩罚值   | 0   | 1   | 3   | 7   | 15  | 31  | 63  |

### 惩罚值公式

```cpp
penalty_value = 2^count - 1        // (1 << count) - 1
```

### 总惩罚费用

```
total_penalty_cost = penalty(target) + penalty(sacrifice)
```

两个物品的惩罚值**相加**，作为操作总成本的组成部分。

## 3. 附魔合并费用

### Java Edition

```
ench_cost = final_level × multiplier
```

- `final_level`：附魔在结果物品上的最终等级
- `multiplier`：附魔的费用乘数（见下文）

**与 Bedrock 的关键区别**：Java 始终基于最终等级计算费用，无论等级是否提升。

### Bedrock Edition

```
ench_cost = (new_level - old_level) × multiplier
```

- `new_level - old_level`：本次操作提升的等级差（新增附魔时 = `level`）
- 等级不变时费用为零

### 附魔升级规则

适用于书籍合并和装备合并：

| 牺牲等级 vs 目标       | 结果等级       |
| ---------------------- | -------------- |
| 牺牲 > 目标            | 提升至牺牲等级 |
| 牺牲 == 目标（未满级） | 目标等级 +1    |
| 牺牲 == 目标（已满级） | 不变           |
| 牺牲 < 目标            | 不变           |

```cpp
// 合并逻辑（略去满级检查）
new_level = (sacrifice_level == target_level)
          ? target_level + 1
          : max(target_level, sacrifice_level);
```

### 新增 vs 升级

当牺牲物品携带目标没有的附魔（新增）：

- Java：`cost = level × multiplier`
- Bedrock：`cost = level × multiplier`（diff = level - 0）

当牺牲物品携带目标已有的附魔（升级）：

- Java：`cost = new_level × multiplier`
- Bedrock：`cost = (new_level - old_level) × multiplier`

### 不兼容附魔

Java 版对附魔冲突的处理：

```
incompatible_penalty = count(incompatible_enchants) × 1
```

- 冲突附魔不会被转移到结果物品上
- 每个冲突 +1 级
- Bedrock 版无冲突费用，且操作被拒绝

### 不适用附魔

- 牺牲物品上的魔咒若对目标物品类型不适用，则被忽略
- 不产生费用，不转移

## 4. 附魔乘数表

乘数定义在 `EnchInfo::multiplier` 字段中。书本乘数自动派生：

```cpp
// Ench::get_multiplier(is_book)
book_mult = max(1, item_mult >> 1);    // item_mult 右移 1 位，最小 1
```

| 魔咒                 | 物品乘数 | 书本乘数 |
| -------------------- | -------- | -------- |
| 效率 / 保护 / 锋利等 | 1        | 1        |
| 击退等               | 2        | 1        |
| 抢夺 / 火焰附加等    | 4        | 2        |
| 荆棘等               | 8        | 4        |

## 5. 耐久修复费用

### 双物品合并修复

```
repair_cost = 2
```

两个同种装备合并时，结果耐久 = `target.durability + sacrifice.durability + max_durability × 12%`。费用 +2 级。

### 材料修复（单物品 + 材料）

```
repair_cost = 1 × unit
```

- 每单位材料恢复最多 25% 总耐久度
- 每次操作费用 +1 级

注意：ignore_repair_cost 配置可忽略修复费用。

## 6. 费用上限

「过于昂贵！」上限为 **39 级**。

```cpp
final_cost = min(raw_cost, 39);    // 当 ignore_cost_cap == false
```

ignore_cost_cap 配置可禁用于模组环境。

## 7. 总费用公式

```
总费用 = 附魔合并费用
       + 前次工作惩罚(目标) + 前次工作惩罚(牺牲)
       + 更名费用 (if applicable, 1级)
       + 耐久修复费用 (if applicable, 1或2级)
       + 不兼容惩罚 (Java: 冲突数 × 1, Bedrock: 0)
```

费用上限 39 级在最后应用。

## 8. 物品合法性

### 可锻造组合

| 目标   | 牺牲   | 可锻造                    |
| ------ | ------ | ------------------------- |
| 装备   | 装备   | ✅ 合并附魔 + 耐久修复    |
| 装备   | 附魔书 | ✅ 合并附魔（无耐久修复） |
| 装备   | 材料   | ✅ 仅耐久修复             |
| 附魔书 | 附魔书 | ✅ 合并附魔               |
| 附魔书 | 装备   | ❌                        |
| 附魔书 | 材料   | ❌                        |

### 不可锻造情况

```cpp
is_forgeable(a, b)
    = a.is_equipment() || (a.is_book() && b.is_book());
```

## 9. Java vs Bedrock 差异汇总

| 规则       | Java                     | Bedrock                  |
| ---------- | ------------------------ | ------------------------ |
| 费用计算   | final_level × multiplier | (new - old) × multiplier |
| 不兼容惩罚 | +1/冲突                  | 无，操作被拒绝           |
| 费用上限   | 39 级                    | 39 级                    |

## 10. 测试用例参考

以下典型组合用于验证算法正确性：

| 场景        | 目标        | 牺牲           | 结果等级 | Java 费用    |
| ----------- | ----------- | -------------- | -------- | ------------ |
| 新增附魔    | 无附魔剑    | 锋利 V 书      | 锋利 V   | 1 × 5 = 5    |
| 升级        | 锋利 III 剑 | 锋利 V 书      | 锋利 V   | 1 × 5 = 5    |
| 同级升级    | 锋利 IV 剑  | 锋利 IV 书     | 锋利 V   | 1 × 5 = 5    |
| 高级 + 低级 | 锋利 IV 剑  | 锋利 II 书     | 锋利 IV  | 1 × 4 = 4    |
| 合并书本    | 锋利 IV 书  | 锋利 III 书    | 锋利 IV  | 1 × 4 = 4    |
| 不兼容      | 锋利 V 剑   | 节肢杀手 IV 书 | 锋利 V   | +1（无转移） |

前次工作惩罚为 0 时的预期费用。
