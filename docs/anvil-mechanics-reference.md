# 铁砧机制算法参考

> 依据 Minecraft Wiki (minecraft.wiki) 及 JE 源码整理，对齐 Java Edition 最新版本。
> 本文件作为算法层开发的权威参考，所有 forge 逻辑应与此保持一致。
> 参见：docs/MPMCQueue.md 了解算法引擎使用的并发原语。
> 代码实现：src/algorithm/forge/ForgeEngine.h（Vanilla forge engine，所有子操作可通过继承覆写）。
> 接口定义：src/algorithm/forge/IForgeEngine.h（虚接口 + 默认实现）。

---

## 1. 形式化模型

### 物品状态

物品 $S$ 定义为五元组：

$$S = (\text{type},\ d,\ E,\ N,\ P)$$

| 分量 | 含义 | 类型 |
|------|------|------|
| $\text{type}$ | 物品类型与材料（如钻石剑、附魔书） | `const Equipment*` |
| $d$ | 耐久度，$0\le d\le D_{\max}$ | `int32_t` |
| $E$ | 附魔集合，形如 $\{(e_i,\ell_i)\}$ | `EnchSet` |
| $N$ | 物品名称 | `std::string` |
| $n$ | 累积惩罚计数（RepairCost 的指数），惩罚值 $2^n-1$，存于 `prior_penalty` | `int32_t` |

### 操作定义

一次铁砧操作接收目标物品 $S_A$ 和牺牲物品 $S_B$，产生新物品 $S_C$ 并消耗经验等级：

$$(S_A, S_B) \xrightarrow{\text{forge}} S_C,\quad \text{Cost}(S_A,S_B) \in \mathbb{N}$$

操作类型：
1. **附魔合并** — $B$ 为附魔书或同类型装备
2. **耐久修复** — $B$ 为修复材料或同类型装备
3. **更名** — 可附加在任何操作中，固定 +1 级，不增加 $P$

### 成本函数

$$\text{Cost}(S_A,S_B) = P_A + P_B + R_E + R_D + C_{\text{ench}}$$

| 项 | 含义 | 代码对应 |
|----|------|---------|
| $P_A + P_B$ | 前次工作惩罚和 | `get_penalty_cost(a) + get_penalty_cost(b)` |
| $R_E$ | 更名费用 | 1（若更名）或 0 |
| $R_D$ | 耐久修复费用 | 2（装备+装备）或 1（装备+材料）或 0 |
| $C_{\text{ench}}$ | 附魔合并费用 | `combine_enchantments()` |

### 累积惩罚更新

操作次数 $n$ 更新：$n_C = \max(n_A, n_B) + 1$

对应惩罚值：$P = 2^n - 1$，或由惩罚值直接计算：

$$P_C = 2 \cdot \max(P_A, P_B) + 1$$

### 状态变换

`forge()` 实现状态变换 $S_A \rightarrow S_C$：
- `S_C.E` = 合并后的附魔集合
- `S_C.n` = $\max(n_A, n_B) + 1$
- `S_C.d` = 合并后的耐久度
- `S_C.type` = `S_A.type`

`forge_into()` 等价于上述变换的原地版本（直接修改 `S_A` 作为 `S_C`）。

---

## 2. 前次工作惩罚（Prior Work Penalty）

### 惩罚计数

物品每次经历铁砧操作（更名除外），惩罚计数 $n$ 增加：

$$n_C = \max(n_A, n_B) + 1$$

### 惩罚值

惩罚值 $P$ 与计数 $n$ 的关系：

$$P = 2^n - 1$$

等价形式（直接由惩罚值计算）：

$$P_C = 2 \cdot \max(P_A, P_B) + 1$$

| $n$ | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|---|---|---|---|---|---|---|
| $P$ | 0 | 1 | 3 | 7 | 15 | 31 | 63 |

### 总惩罚费用

```
total_penalty_cost = penalty(target) + penalty(sacrifice)
```

两个物品的惩罚值**相加**，作为操作总成本的组成部分。

### 实现对应

```cpp
// ForgeEngine::penalty_cost() — IForgeEngine 默认实现
int32_t IForgeEngine::penalty_cost(int8_t ppn) const noexcept { return (1 << ppn) - 1; }

// ForgeEngine::forge_into() 中的惩罚计算
cost += penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);

// 结果物品惩罚计数（forge_into 原地修改 target）
target.ppn = 1 + std::max(target.ppn, sacrifice.ppn);
```

---

## 3. 附魔合并费用

### Java Edition

```
ench_cost = final_level × multiplier
```

- `final_level`：附魔在结果物品上的最终等级
- `multiplier`：附魔的费用乘数（见乘数表）

**与 Bedrock 的关键区别**：Java 始终基于最终等级计算费用，无论等级是否提升。

### Bedrock Edition

```
ench_cost = (new_level - old_level) × multiplier
```

- `new_level - old_level`：本次操作提升的等级差（新增附魔时 = `level`）
- 等级不变时费用为零

### 附魔升级规则

适用于书籍合并和装备合并：

| 牺牲等级 vs 目标 | 结果等级 |
|-----------------|---------|
| 牺牲 > 目标 | 提升至牺牲等级 |
| 牺牲 == 目标（未满级） | 目标等级 +1 |
| 牺牲 == 目标（已满级） | 不变 |
| 牺牲 < 目标 | 不变 |

```cpp
// ForgeEngine::forge_into() — 等级合并 max-or-increment 语义
// (old_level == se.level) ? min(old_level + 1, max_lvl) : max(old_level, se.level)
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
- Bedrock 版无冲突费用，冲突附魔被忽略，操作正常进行

### 实现对应

```cpp
// ForgeEngine::forge_into() — Java 分支（简化，完整 API 见 IForgeEngine）
if (plat == MCE::Java)
    cost += mult * new_level;                // final_level × multiplier
else
    cost += mult * (new_level - old_level);  // Bedrock: diff × multiplier

// 不兼容惩罚
cost += (plat == MCE::Java && conflict) ? 1 : 0;

// 乘数查找：reg[id].mul — compact::EnchInfo 直接字段访问
// 书本乘数：reg[id].mul_b (数据加载时预计算)
// 简言之：sac_is_book ? reg[id].mul_b : reg[id].mul
```

---

## 4. 附魔乘数表

乘数定义在 `EnchInfo::multiplier` 字段中。书本乘数自动派生：

```cpp
book_mult = max(1, item_mult >> 1);    // item_mult 右移 1 位，最小 1
```

| 魔咒 | 物品乘数 | 书本乘数 |
|------|---------|---------|
| 效率 / 保护 / 锋利等 | 1 | 1 |
| 击退等 | 2 | 1 |
| 抢夺 / 火焰附加等 | 4 | 2 |
| 荆棘等 | 8 | 4 |

---

## 5. 耐久修复费用

### 双物品合并修复

```
repair_cost = 2    （仅目标耐久未满时）
```

两个同种装备合并时，结果耐久 = `target.durability + sacrifice.durability + max_durability × 12%`。若目标耐久已满，无修复费用。费用 +2 级仅当目标耐久未满时适用。

### 材料修复（单物品 + 材料）

```
repair_cost = 1 × unit
```

- 每单位材料恢复最多 25% 总耐久度
- 每次操作费用 +1 级

注意：`ignore_repair_cost` 配置项控制是否计入修复费用（默认 false 表示计入）。当前实现仅覆盖 equip+equip 修复（+2 级），equip+material 修复尚未实现。

---

## 6. 费用上限

「过于昂贵！」上限为 **39 级**。

```cpp
final_cost = min(raw_cost, 39);    // 当 ignore_cost_cap == false
```

`ignore_cost_cap` 配置可禁用于模组环境。

---

## 7. 总费用公式

$$\text{总费用} = C_{\text{ench}} + P_A + P_B + R_E + R_D$$

| 项 | 含义 |
|----|------|
| $C_{\text{ench}}$ | 附魔合并费用（含不兼容惩罚：Java +1/冲突，Bedrock +0） |
| $P_A + P_B$ | 前次工作惩罚（目标+牺牲） |
| $R_E$ | 更名费用（1 或 0） |
| $R_D$ | 耐久修复费用（2 或 1 或 0） |

费用上限 39 级在最后应用：

$$\text{最终费用} = \min(\text{总费用}, 39)$$

---

## 8. 物品合法性

### 可锻造组合

| 目标 | 牺牲 | 可锻造 |
|------|------|-------|
| 装备 | 装备 | ✅ 合并附魔 + 耐久修复 |
| 装备 | 附魔书 | ✅ 合并附魔（无耐久修复） |
| 装备 | 材料 | ✅ 仅耐久修复 |
| 附魔书 | 附魔书 | ✅ 合并附魔 |
| 附魔书 | 装备 | ❌ |
| 附魔书 | 材料 | ❌ |

### 实现对应

```cpp
// ForgeEngine::is_forgeable
bool ForgeEngine::is_forgeable(const compact::Item& a, const compact::Item& b) const noexcept {
    return a.type == compact::ItemType::Equip
        || (a.type == compact::ItemType::Book && b.type == compact::ItemType::Book);
}
```

---

## 9. Java vs Bedrock 差异汇总

| 规则 | Java | Bedrock |
|------|------|---------|
| 费用计算 | final_level × multiplier | (new - old) × multiplier |
| 不兼容惩罚 | +1/冲突 | 无，冲突附魔被忽略 |
| 费用上限 | 39 级 | 39 级 |

---

## 10. 操作示例：完整成本计算

### 示例：三书合并到剑

牺牲物品为附魔书时，使用书本乘数（普通乘数右移一位，最小 1）。
锋利 V：书本乘数 1；耐久 III：书本乘数 1；抢夺 III：书本乘数 2。

目标：钻石剑 {锋利 V, 耐久 III, 抢夺 III}
初始：无附魔剑 + 锋利 V 书 + 耐久 III 书 + 抢夺 III 书

**Step 1：合并锋利 V 书 + 耐久 III 书**
- 惩罚：均为 0 → 成本 $0+0=0$
- 附魔成本：$5\times1 + 3\times1 = 5 + 3 = 8$
- 总费用：$8$
- 结果惩罚：$2\cdot\max(0,0)+1 = 1$

**Step 2：合并结果 + 抢夺 III 书**
- 惩罚：$1 + 0 = 1$
- 附魔成本：$5\times1 + 3\times1 + 3\times2 = 5 + 3 + 6 = 14$
- 总费用：$1 + 14 = 15$
- 结果惩罚：$2\cdot\max(1,0)+1 = 3$

**Step 3：合并三合一书 + 无附魔剑**
- 惩罚：$3 + 0 = 3$
- 附魔成本：$5\times1 + 3\times1 + 3\times2 = 5 + 3 + 6 = 14$
- 剑无损伤，无修复费用
- 总费用：$3 + 14 = 17$
- 结果惩罚：$2\cdot\max(3,0)+1 = 7$

**总消耗：** $8 + 15 + 17 = 40$ 级

### 测试用例参考

| 场景 | 目标 | 牺牲 | 结果等级 | Java 费用 |
|------|------|------|---------|----------|
| 新增附魔 | 无附魔剑 | 锋利 V 书 | 锋利 V | 1 × 5 = 5 |
| 升级 | 锋利 III 剑 | 锋利 V 书 | 锋利 V | 1 × 5 = 5 |
| 同级升级 | 锋利 IV 剑 | 锋利 IV 书 | 锋利 V | 1 × 5 = 5 |
| 高级 + 低级 | 锋利 IV 剑 | 锋利 II 书 | 锋利 IV | 1 × 4 = 4 |
| 合并书本 | 锋利 IV 书 | 锋利 III 书 | 锋利 IV | 1 × 4 = 4 |
| 不兼容 | 锋利 V 剑 | 节肢杀手 IV 书 | 锋利 V | +1（无转移）|

前次工作惩罚为 0 时的预期费用。
