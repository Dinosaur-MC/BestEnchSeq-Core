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


## 数据流

```
数据文件 (JSON/CSV)
  → EnchInfoParser::parse()     → RawEnchantment[] (string-based)
  → EquipmentParser::parse()     → RawEquipment[]   (string-based)
                                  → 下游: RegistryResolver / RawTypeAdapter

CLI args
  → CLIParser::parse()           → ParsedArg[]
  → parse_cli()                  → CLIConfig
  → EnchParser / ItemParser      → EnchantmentSpec[] / TargetSpec
                                  → 下游: build_target / build_enchset / ItemResolver
```

## 开发说明

- 新增解析器时保持零注册表依赖，返回 string-based 中间类型
- `ParserUtilsDomain.hpp` 包含解析器和 domain 层共享的工具函数
- 所有解析错误在解析阶段报告，不在类型转换阶段
