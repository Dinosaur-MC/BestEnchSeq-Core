# 适配器层（`adapters/`）

## 设计目的

适配器层是 domain 类型和 compact 类型之间的边界转换器。算法层只认 compact 类型，I/O 层只认 domain 类型，适配器负责双向翻译。

```
Domain (边界 I/O)          Adapter                Compact (算法层)
─────────────────       ──────────              ─────────────────
ParsedInput              CompactAdapter          AlgorithmInput
EnchSolution             → apply()               compact::EnchSolution
ItemStack                → recall()              compact::Item
RawEnchInfo / RawEquipment  → from_domain()
                         RegistryResolver        compact::EnchReg
                         → resolve_*()
```

---

## CompactAdapter

核心边界适配器，负责 domain ↔ compact 的双向转换。

```cpp
class CompactAdapter {
    // domain → compact（验证 + 剪枝 + 转换）
    static AlgorithmInput apply(
        const ItemStack& target_item,
        const EnchSet& original_ench,
        const ItemCollection& available_items,
        const ForgeConfig& config,
        const EnchantmentRegistry& global_registry);

    // compact → domain（恢复全局 ID + 构建 EnchSolutions）
    static std::vector<EnchSolution> recall(
        const AlgorithmOutput& output,
        const AlgorithmInput& input,
        const EnchSet& original_ench,
        const ItemStack& target_item,
        const ItemCollection& available_items);

    // 单物品转换
    static compact::Item from_domain(const ItemStack& item,
                                     const EnchantmentRegistry& reg);
    static ItemStack to_domain(const compact::Item& item,
                               const EquipmentRegistry& eq_reg);
};
```

### apply() 流程

1. **验证**：检查输入合法性（目标装备存在、可用物品不为空等）
2. **剪枝**：通过 `EnchantmentRegistry::create_subset()` 从全局注册表派生出只包含适用附魔的 compact 子集
3. **转换**：所有 `ItemStack` → `compact::Item`（ID 重映射到 dense 索引）

### recall() 流程

1. 算法层输出 `compact::EnchSolution` 序列
2. 通过 `compact::EnchReg` 的逆向映射恢复全局附魔 ID
3. 将 compact 类型还原为 domain 类型的 `EnchSolution` / `ItemStack`

---

## RegistryResolver

字符串引用 → int32_t ID 的解析器。

```cpp
struct RegistryResolver {  // 全静态
    // Raw → domain
    static std::vector<EnchInfo> resolve_ench_info(
        const std::vector<RawEnchInfo>& raw,
        const EquipmentCategoryRegistry& cat_reg);
    static std::vector<Equipment> resolve_equipment(
        const std::vector<RawEquipment>& raw,
        const EquipmentCategoryRegistry& cat_reg);

    // 名称 → ID
    static int32_t resolve_ench_id(std::string_view name,
                                   const EnchantmentRegistry& ench_reg);
    static int32_t resolve_ench_id(std::string_view ns,
                                   std::string_view id,
                                   const EnchantmentRegistry& ench_reg);

    // 数据合并
    static void merge_raw_ench_info(
        std::vector<RawEnchInfo>& base,
        const std::vector<RawEnchInfo>& extra);
    static void merge_raw_equipment(
        std::vector<RawEquipment>& base,
        const std::vector<RawEquipment>& extra);
};
```

resolve_ench_id 的查找顺序：先试裸名称，再试 `"minecraft:"` 前缀。未找到时返回 -1（非 throwing 变体）。

---

## OutputFormatter

算法输出 → 可读文本。

```cpp
class OutputFormatter {  // 全静态
    static std::string format_verbose(
        const std::vector<EnchSolution>& solutions,
        const EnchantmentRegistry& ench_reg,
        const EquipmentCategoryRegistry& cat_reg,
        std::string_view mode_name);

    static std::string format_compact(
        const std::vector<EnchSolution>& solutions,
        const EnchantmentRegistry& ench_reg,
        const EquipmentCategoryRegistry& cat_reg,
        std::string_view mode_name);

    static std::string format_json(
        const std::vector<EnchSolution>& solutions,
        const EnchantmentRegistry& ench_reg,
        const EquipmentCategoryRegistry& cat_reg,
        std::string_view mode_name);

    // JSON → domain 反序列化
    static std::vector<EnchSolution> parse_json(
        const std::string& json_str,
        const EnchantmentRegistry& ench_reg,
        const EquipmentCategoryRegistry& cat_reg);

    static void clear_cache();
};
```

| 模式 | 用途 |
|---|---|
| `format_verbose` | 详细文字输出，每个步骤的锻造消耗和结果 |
| `format_compact` | 紧凑单行输出，适合快速查看 |
| `format_json` | 结构化 JSON，供外部工具消费 |

---

## EnchSerializer

附魔/装备数据的序列化工具（JSON / CSV / 官方格式）。

```cpp
class EnchSerializer {
    // JSON I/O
    static bool ench_info_to_json(const std::string& path,
                                   const std::vector<EnchInfo>& data);
    static bool equipment_to_json(const std::string& path,
                                   const std::vector<Equipment>& data);

    // CSV I/O
    static bool ench_info_to_csv(const std::string& path,
                                  const std::vector<EnchInfo>& data);
    static bool equipment_to_csv(const std::string& path,
                                  const std::vector<Equipment>& data);

    // Minecraft 官方数据导出
    static bool ench_info_to_mc_official(const std::string& path,
                                          const std::vector<EnchInfo>& data);
};
```

---

## 性能说明

| 操作 | 开销 | 说明 |
|---|---|---|
| `CompactAdapter::apply()` | 一次注册表子集构建 | 算法开始前执行一次 |
| `CompactAdapter::recall()` | 一次逆向映射 | 算法结束后执行一次 |
| `from_domain()` / `to_domain()` | 每个物品 O(n) | n = 附魔数量 |
| `OutputFormatter::format_*` | 字符串格式化 | 仅在最终输出时调用 |
