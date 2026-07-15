# 解析器层（`parsers/`）

## 设计原则

解析器**不依赖任何注册表**。解析只生成字符串引用（`RawEnchInfo` / `RawEquipment`），所有 string→ID 解析在后续的 `RegistryResolver` 中统一完成。

```
数据文件 (JSON/CSV)
  → EnchInfoParser::parse()     → RawEnchInfo (string-based)
  → EquipmentParser::parse()    → RawEquipment (string-based)
  → RegistryResolver::resolve() → domain types (int32_t IDs)
```

---

## CLIParser

通用键值对 CLI 解析器，**零业务逻辑**。

```cpp
struct ParsedArg {
    std::string key;
    std::string value;
    bool is_flag = false;
};

class CLIParser {  // 全静态
    static std::vector<ParsedArg> parse(int argc, char* argv[]);
};
```

支持的格式：

| 格式 | 示例 |
|---|---|
| `--key=value` | `--target=diamond_sword` |
| `--key value` | `--algo astar` |
| `--flag` | `--verbose` |
| 短参数 | `-h` → 自动映射为 key=`help`, is_flag=true |

无参数时返回空 vector，不会出错。

---

## EnchInfoParser

附魔数据文件解析器。

```cpp
class EnchInfoParser {
    static std::vector<RawEnchInfo> parse(const json::Json& data);
};
```

- 输入：JSON 格式的附魔数据文件（如 `vanilla.json`）
- 输出：`RawEnchInfo` 向量，所有分类引用仍为字符串
- 支持多平台（Java / Bedrock）标记

---

## EquipmentParser

装备数据文件解析器。

```cpp
class EquipmentParser {
    static std::vector<RawEquipment> parse(const json::Json& data);
};
```

- 输入：JSON 格式的装备数据文件
- 输出：`RawEquipment` 向量，`category` 字段仍为字符串
- 未知分类名保持原样传递给 RegistryResolver 处理

---

## InputParser

用户输入解析器，组装完整的算法输入。

```cpp
struct ParsedInput {
    MCE platform;                   // Java / Bedrock
    EnchSet original_ench;          // 装备已有附魔
    ItemStack target_item;          // 目标装备
    ItemCollection available_items; // 可用物品（书 + 材料）
};

class InputParser {  // 全静态
    // 从文件解析物品栏
    static ItemCollection parse_inventory(
        const std::string& path,
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& equip_reg);

    // 从 CLI spec 构造目标物品
    static ItemStack build_target(
        const std::string& target_spec,
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& equip_reg);

    // 转换附魔规格字符串为 EnchSet
    static EnchSet build_wanted_enchset(
        const std::string& wanted,
        const EnchantmentRegistry& ench_reg);

    // 完整管线：CLI config → ParsedInput
    static ParsedInput assemble_input(
        const CLIConfig& cli_config,
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& equip_reg);

    // 为每个目标附魔等级生成对应的书
    static ItemCollection generate_books(
        const EnchSet& wanted,
        const EnchSet& existing);
};
```

与前面三个不同，`InputParser` 调用注册表（因为需要将用户输入字符串解析为具体的 ID）。
它是解析器层和适配器层之间的桥梁。

---

## 数据流总览

```
CLI 参数 ──→ CLIParser ──→ ParsedArg[]
                               │
     附魔 JSON ──→ EnchInfoParser ──→ RawEnchInfo[]
     装备 JSON ──→ EquipmentParser ──→ RawEquipment[]
                               │
                               ▼
                         RegistryResolver
                               │
                               ▼
                         EnchantmentRegistry
                         EquipmentRegistry
                               │
    ParsedArg[] ──────────────►│
                               ▼
                         InputParser::assemble_input()
                               │
                               ▼
                         ParsedInput
                               │
                               ▼
                         CompactAdapter::apply()
                               │
                               ▼
                         AlgorithmInput (compact)
```

## 开发说明

- 新增解析器时保持零注册表依赖，返回 string-based 中间类型
- `ParserUtilsDomain.hpp` 包含解析器和 domain 层共享的工具函数
- 所有解析错误在解析阶段报告，不在类型转换阶段
