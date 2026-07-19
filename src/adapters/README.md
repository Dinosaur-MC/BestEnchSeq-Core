# 适配器层（`adapters/`）

## 设计目的

适配器层是 domain 类型和 compact 类型之间的边界转换器。算法层只认 compact 类型，I/O 层只认 domain 类型，适配器负责双向翻译。

```
Domain (边界 I/O)          Adapter                Compact (算法层)
─────────────────       ──────────              ─────────────────
ResolvedInput            CompactAdapter          AlgorithmInput
EnchSolution             → recall()              compact::EnchSolution
ItemStack                → from_domain()         compact::Item
                         → to_domain()
                         RawTypeAdapter          Domain Registries
                         → resolve()             (EnchantmentRegistry, etc.)
```

---

## CompactAdapter

核心边界适配器，负责 domain ↔ compact 的双向转换。全部为静态方法。

- `apply(resolved, global_registry)` — domain → compact（验证 + 剪枝 + 转换）
- `recall(output, input, ...)` — compact → domain（恢复全局 ID + 构建 EnchSolutions）
- `from_domain(item, reg)` / `to_domain(item, reg)` — 单物品转换

### apply() 流程

1. **验证**：检查 compact 级约束（prior_penalty [0,31]、durability [1, max]、ID 范围、等级范围）
2. **剪枝**：通过 `EnchantmentRegistry::create_subset()` 从全局注册表派生出只包含适用附魔的 compact 子集
3. **转换**：所有 `ItemStack` → `compact::Item`（ID 重映射到 dense 索引）

### recall() 流程

1. 算法层输出 `compact::EnchSolution` 序列
2. 通过 `compact::EnchReg` 的逆向映射恢复全局附魔 ID
3. 将 compact 类型还原为 domain 类型的 `EnchSolution` / `ItemStack`

---

## RawTypeAdapter

数据文件 → 领域注册表的一次性初始化桥梁。

流程：收集类别 → `cat_reg.initialize()` → `RegistryResolver::resolve_equipment()` → `eq_reg.initialize()` → `RegistryResolver::resolve_ench_info()` → `ench_reg.initialize()`

> RegistryResolver 已移至 `resolvers/` 层，负责 string → int32_t ID 转换。

---

## OutputFormatter

算法输出 → 可读文本。

全静态方法。

- `format_verbose` — 详细文字输出，每个步骤的锻造消耗和结果
- `format_compact` — 紧凑单行输出，适合快速查看
- `format_json` — 结构化 JSON，供外部工具消费
- `parse_json` — JSON → domain 反序列化

| 模式 | 用途 |
|---|---|
| `format_verbose` | 详细文字输出，每个步骤的锻造消耗和结果 |
| `format_compact` | 紧凑单行输出，适合快速查看 |
| `format_json` | 结构化 JSON，供外部工具消费 |

---

## EnchSerializer

附魔/装备数据的序列化工具。支持 JSON / CSV / MC 官方格式三种输出。

---

## 性能说明

| 操作 | 开销 | 说明 |
|---|---|---|
| `CompactAdapter::apply()` | 一次注册表子集构建 | 算法开始前执行一次 |
| `CompactAdapter::recall()` | 一次逆向映射 | 算法结束后执行一次 |
| `from_domain()` / `to_domain()` | 每个物品 O(n) | n = 附魔数量 |
| `OutputFormatter::format_*` | 字符串格式化 | 仅在最终输出时调用 |
