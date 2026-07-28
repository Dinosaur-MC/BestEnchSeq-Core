# 锻造引擎（`src/domain/algorithm/forge_engine/`）

## 架构

```
IForgeEngine（纯虚基类）
  └── ForgeEngine（原版 Minecraft 实现）
        └── 用户可继承 ForgeEngine 重写特定子操作 → mod 支持
```

## IForgeEngine 接口

### 核心操作

| 方法 | 语义 | 说明 |
|---|---|---|
| `forge_into(target, sacrifice, reg)` | 原地锻造 | 修改 target，返回成本。JE/BE 分支在此 |
| `forge(target, sacrifice, reg)` | 非修改锻造 | 拷贝 target → forge_into → 返回 {result, cost} |
| `is_forgeable(target, sacrifice)` | 可锻造性 | 当前仅 Equip+any 或 Book+Book |
| `pure_forge_into(target, sacrifice, reg)` | 纯状态合并 | 跳过成本计算，用于 simulate() |

### 子操作（可覆写）

这些是 `forge_into` 内调用的虚方法，通过虚分派传播，**覆写即可改变行为**：

| 方法 | 默认实现 | 覆写场景 |
|---|---|---|
| `penalty_cost(ppn)` | `(1 << ppn) - 1`（上限 30 → INT32_MAX） | 惩罚公式修改 |
| `estimate_forge_cost(target, sacrifice, reg)` | 惩罚 + 附魔乘数（**不** 应用 cap） | 启发式重写 |

### 配置

`ForgeConfig` 控制平台差异和开关：

```cpp
struct ForgeConfig {
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false;
    MCE platform             = MCE::Java;   // Java / Bedrock
};
```

平台影响：
- 附魔合并成本公式：JE = `mult × new_level`，BE = `mult × (new_level - old_level)`
- 冲突惩罚：JE +1，BE +0

## ForgeEngine 原版行为

```
forge_into(target, sacrifice, reg):
  成本 = P_A + P_B          (penalty_cost，受 ignore_penalty_cost 控制)
        + C_ench            (附魔合并，JE/BE 公式不同)
        + conflict_penalty  (JE only: +1 per conflict)
  惩罚更新: target.ppn = max(target.ppn, sacrifice.ppn) + 1
  附魔合并: 相同附魔取 max 或 +1（同级时）；冲突附魔跳过
  耐久度修复: equip + equip 合并，受 ignore_repair_cost 控制
  成本上限: 原版上限为 39 级，算法搜索中通过 estimate_forge_cost / cost_so_far 约束
            由 ForgeEngine 直接返回原始成本，外层 Pipeline 和 OutputFormatter 负责标记超限
```

## 扩展指南（Mod 支持）

```cpp
// 示例：自定义成本公式
class ModForgeEngine : public ForgeEngine {
public:
    using ForgeEngine::ForgeEngine;

    int32_t penalty_cost(int8_t ppn) const noexcept override {
        return ppn * 2;  // 线性惩罚，非指数
    }
};
```

覆盖的子操作会自动在 `forge_into()` / `forge()` / `estimate_forge_cost()` 中生效。
