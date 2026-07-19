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

## ItemResolver（resolvers/ 层）

领域层输入预处理器。接收已解析的 CLI 规格 + 注册表，进行基础验证后装配算法输入。

```cpp
struct ResolvedInput {
    ItemStack target_item;       // 已验证的装备
    EnchSet source_ench;         // 已有附魔（--source）
    EnchSet target_ench;         // 目标附魔（已验证适用性 + 无冲突）
    ItemCollection books;        // 生成的毕业附魔书
};

struct ItemResolver {
    static ResolvedInput resolve(
        const ItemStack& target_item,
        const EnchSet& source_ench,
        const EnchSet& target_ench,
        const EnchantmentRegistry& ench_reg
    );
};
```

职责：
1. 校验目标附魔对装备的适用性
2. 校验附魔间 exclusive_set 无冲突
3. 计算 diff = target_ench − source_ench
4. 为 diff 生成毕业附魔书

---

## 数据流总览

```
                      ┌─ EnchInfoParser ─→ TagResolver
                      │     │
                      │     ▼
                      │  RawTypeAdapter
                      │     │
                      │     ▼
                      │  Domain registries (EnchantmentRegistry, etc.)
                      │
CLI ─→ CLIParser ─→ parse_cli() ─→ CLIConfig
  │
  ├─ EnchParser::parse(source)  → EnchantmentSpec[]
  ├─ ItemParser::parse(target)  → TargetSpec
  ├─ build_target / build_enchset (cli helpers, 注册表查询)
  │
  └─ ItemResolver::resolve() → ResolvedInput
       │
       ▼
  CompactAdapter::apply() → AlgorithmInput (compact)
       │
       ▼
  AlgorithmExecutor → IAlgorithm::execute(input)
```

## 开发说明

- 新增解析器时保持零注册表依赖，返回 string-based 中间类型
- `ParserUtilsDomain.hpp` 包含解析器和 domain 层共享的工具函数
- 所有解析错误在解析阶段报告，不在类型转换阶段
