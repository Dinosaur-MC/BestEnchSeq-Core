# 数据源与提取方法

> 版本：2.0
> 最后更新：2026-07-29

---

## 目录

1. [概述](#1-概述)
2. [数据流水线](#2-数据流水线)
3. [脚本架构](#3-脚本架构)
4. [原始来源：Minecraft 客户端 Jar](#4-原始来源minecraft-客户端-jar)
5. [步骤 1：本地化数据](#5-步骤-1本地化数据)
6. [步骤 2：标签系统](#6-步骤-2标签系统)
7. [步骤 3：魔咒数据提取](#7-步骤-3魔咒数据提取)
8. [步骤 4：装备数据提取](#8-步骤-4装备数据提取)
9. [步骤 5：物品附魔值](#9-步骤-5物品附魔值)
10. [步骤 6：Limited_Level 计算](#10-步骤-6limited_level-计算)
11. [步骤 7：适用性模型（supported_items ∩ 物品 tag）](#11-步骤-7适用性模型supported_items-物品-tag)
12. [语言资源提取](#12-语言资源提取)
13. [输出格式](#13-输出格式)
14. [C++ 侧解析与 i18n](#14-c-侧解析与-i18n)
15. [条目级数据来源参考](#15-条目级数据来源参考)

---

## 1. 概述

`data/builtin/vanilla.json` 是 BestEnchSeq-Core 的内建原版数据文件，包含：

| 数据类别 | 条目数 | 说明 |
|---------|:------:|------|
| **enchantments** | 43 | 全部原版魔咒的注册信息 |
| **equipments** | 77 | 可锻造的装备类型 |
| **tags** | 29 | 魔咒冲突组和物品分类标签 |

此外，`data/i18n/minecraft/` 包含从 Mojang 资源服务器提取的翻译数据：

| 文件 | 条目 | 涵盖范围 |
|------|:----:|----------|
| `en_US.json` | 803 | 44 魔咒 + 759 物品 |
| `zh_CN.json` | 823 | 44 魔咒 + 779 物品 |

此文件由 `scripts/vanilla/` 模块包从 Mojang 官方资源自动提取生成，**不需要手动维护**。

---

## 2. 数据流水线

```
Mojang 版本清单 (version_manifest.json)
        │
        ├────→ Asset Index (version_manifest_v2.json)
        │              │
        │              ▼
        │       minecraft/lang/<locale>.json (哈希索引)
        │              │
        │              ▼
        │       resources.download.minecraft.net/<hash[:2]>/<hash>
        │              │
        │              ▼
        │       data/i18n/minecraft/<locale>.json   ← 多语言翻译
        │
        ▼
    下载最新版客户端 jar  →  ZIP 解压到 res/vanilla/
        │
        ▼
    ┌──────────────────────────────────────┐
    │  提取步骤                            │
    │                                      │
    │  1. 本地化文件 → 魔咒显示名称        │
    │  2. 标签 JSON  → 标签展开和分类      │
    │  3. 魔咒 JSON  → 魔咒注册信息        │
    │  4. 物品标签    → 装备列表           │
    │  5. 字节码分析 → 耐久度值            │
    │  6. 源码常量    → 物品附魔值         │
    │  7. 成本模拟    → limited_level      │
    │  8. 物品分组    → 装备显示类别（短名，不参与适用性判定）│
    └──────────────────────────────────────┘
        │
        ▼
    data/builtin/vanilla.json
```

---

## 3. 脚本架构

提取脚本已重构为模块化包：

```
scripts/
├── get_vanilla_data.py          ← 薄封装 → vanilla.cli
├── download_mc_lang.py          ← 薄封装 → vanilla.lang
│
└── vanilla/                     ← 核心提取逻辑
    ├── cli.py                   CLI + 流程编排
    ├── meta.py                  HTTP 工具、版本清单、Asset Index
    ├── jar.py                   客户端 jar 下载与提取
    ├── enchantment.py           魔咒/装备数据提取、javap 分析
    ├── lang.py                  语言文件资源下载、提取与导出
    └── lang_config.py           语言配置（键前缀、区域映射）
```

| 模块 | 对应旧脚本功能 |
|------|---------------|
| `cli.py` + `meta.py` + `jar.py` | `get_vanilla_data.py` 的下载/提取/元数据部分 |
| `enchantment.py` | `get_vanilla_data.py` 的魔咒/装备分析部分 |
| `lang.py` + `lang_config.py` | `download_mc_lang.py` 的语言文件处理 |

旧入口 `scripts/get_vanilla_data.py` 和 `scripts/download_mc_lang.py` 保留为薄封装，
调用方式不变。

---

## 4. 原始来源：Minecraft 客户端 Jar

### 4.1 版本获取

```python
# meta.py
VERSION_MANIFEST_V1 = "https://launchermeta.mojang.com/mc/game/version_manifest.json"
```

脚本通过 Mojang 官方版本清单获取**最新正式版**（`latest.release`）的下载链接。
下载客户端 jar 后缓存到 `res/vanilla.jar`，避免重复下载。

### 4.2 解压结构

jar 解压到 `res/vanilla/` 后，主要数据目录为：

```
res/vanilla/
├── assets/minecraft/lang/en_us.json    ← 本地化文件
├── data/minecraft/
│   ├── enchantment/*.json              ← 魔咒定义 (data-driven)
│   ├── tags/
│   │   ├── enchantment/*.json          ← 魔咒标签
│   │   └── item/*.json                 ← 物品标签
│   └── item/*.json                     ← 物品定义 (1.21+)
├── net/minecraft/world/item/
│   ├── Items.class                     ← 物品注册字节码
│   ├── ToolMaterial.class              ← 工具材料
│   └── equipment/ArmorMaterials.class  ← 盔甲材料
└── ...
```

### 4.3 MC 1.21+ Data-Driven 格式

自 1.21 起，魔咒采用 data-driven 格式，每个魔咒是一个独立 JSON 文件：

```json
# res/vanilla/data/minecraft/enchantment/sharpness.json
{
  "anvil_cost": 1,
  "max_level": 5,
  "min_cost": { "base": 1, "per_level_above_first": 11 },
  "max_cost": { "base": 21, "per_level_above_first": 11 },
  "exclusive_set": "#minecraft:exclusive_set/damage",
  "supported_items": "#minecraft:enchantable/sharp_weapon",
  "primary_items": "#minecraft:enchantable/melee_weapon",
  "slots": ["mainhand"],
  "weight": 10,
  "effects": { ... }
}
```

---

## 5. 步骤 1：本地化数据

### 来源

`assets/minecraft/lang/en_us.json`（jar 内）

### 方法

在解压目录中递归搜索 `en_us.json` 文件（仅存在一个）。此文件是 Mojang 的
官方英文翻译文件，格式为 `{ "翻译键": "显示文本" }`。

### 用途

将魔咒的技术 ID（如 `sharpness`）映射为显示名称（如 `"Sharpness"`）。

魔咒 JSON 中的 `description.translate` 字段（如 `"enchantment.minecraft.sharpness"`）
作为查找键。

> C++ 运行时不使用此文件。运行时显示名通过 `nsid_display_name()` + `tr()` 从
> Language 系统的翻译表中推导。详见第 14 节。

---

## 6. 步骤 2：标签系统

### 来源

`data/<ns>/tags/enchantment/*.json` 和 `data/<ns>/tags/item/*.json`

### 标签格式

```json
# res/vanilla/data/minecraft/tags/enchantment/non_treasure.json
{
  "values": [
    "minecraft:sharpness",
    "#minecraft:exclusive_set/damage",
    ...
  ]
}
```

`values` 数组中可以是：
- **具体 ID**（如 `"minecraft:sharpness"`）
- **引用标签**（如 `"#minecraft:exclusive_set/damage"`）

### 解析方法

`TagResolver`（C++）和 `resolve_ref()`（Python）都实现了递归展开：

1. 第一遍：收集所有原始标签值（含 `#` 引用）
2. 第二遍：递归展开引用，循环引用检测（`visited` 集合）

### 在 vanilla.json 中的存储

输出仅保留 `enchantment/` 和 `exclusive_set/` 命名空间下的标签：

```json
"tags": {
  "minecraft:enchantment/non_treasure": {
    "values": ["minecraft:sharpness", ...]
  },
  ...
}
```

---

## 7. 步骤 3：魔咒数据提取

### 来源

`data/<ns>/enchantment/<id>.json`

### 提取字段

| 字段 | JSON 源 | 说明 |
|------|---------|------|
| `id` | 文件名 | `{ns}:{filename}`，如 `"minecraft:sharpness"` |
| `name` | `description.translate` → 查 `en_us.json` | 显示名称（vanilla.json 中的快照，运行时由 i18n 重写） |
| `platform` | 硬编码 `"java"` | MC 官方为跨平台数据 |
| `max_level` | `max_level` | 铁砧/附魔台统一最大等级 |
| `limited_level` | **计算所得** | 见第 10 节 |
| `multiplier` | `anvil_cost` | MC 1.21+ 改名，含义相同 |
| `exclusive_set` | `exclusive_set` / `exclusiveSet` | 冲突魔咒 ID 列表，展开 `#` 标签引用 |
| `supported_items` | `supported_items` / `supportedItems` | 适用物品原始引用（`#tag` 或具体物品 ID），**透传不展开**，见第 11 节 |

### 兼容性处理

- `exclusive_set` 在 1.21+ 中可能是字符串或数组，两种都处理
- `supported_items` 同理
- `exclusiveSet`（驼峰）作为 `exclusive_set` 的后备键

---

## 8. 步骤 4：装备数据提取

### 来源

从 `enchantable/*` 标签派生物品列表，而非直接读取装备定义 JSON。

MC 1.21+ 没有独立的"装备注册表"。可附魔的物品通过 `enchantable/<category>` 标签
隐式定义。

### 提取步骤

1. 扫描所有名称包含 `/enchantable/` 的标签
2. 展开标签引用，收集所有引用的物品 ID
3. 从 `load_durability_from_source()` 获取耐久度值
4. 过滤掉无耐久度的物品（耐久度 ≤ 0 时跳过）
5. 通过物品 ID 后缀推导装备**显示**类别（短名，不参与适用性判定，见第 11 节）

### 耐久度获取

耐久度通过两条路径获取：

**路径 A — 字节码反汇编（`javap -c -p`，自动运行）**

脚本自动对 `res/vanilla/net/minecraft/world/item/Items.class` 执行 `javap -c -p`
（JDK 自带反汇编器），解析字节码中的固定模式：

```
ldc String <item_name>       ← 物品名
bipush/sipush <value>        ← 耐久度数值
invokevirtual ...durability... ← 耐久度方法调用
```

> 此分析无需完整的 Java 反编译源代码，仅依赖 jar 中的 `.class` 文件。
> 需要系统安装 JDK 并包含 `javap` 命令。

**路径 B — 硬编码常量（后备）**

工具材料和盔甲材料的耐久度直接根据游戏设计常数计算：

| 材料 | 工具耐久度 | 盔甲单位耐久度 | 盔甲倍率 (头/胸/腿/靴) |
|------|:---------:|:------------:|:---------------------:|
| 木   | 59        | —            | — |
| 石   | 131       | —            | — |
| 铁   | 250       | 15           | 11/16/15/13 |
| 钻石 | 1561      | 33           | 同上 |
| 金   | 32        | 7            | 同上 |
| 下界合金 | 2031  | 37           | 同上 |
| 皮革 | —         | 5            | 同上 |
| 锁链 | —         | 15           | 同上 |
| 龟壳 | —         | 25           | 仅头盔 |

盔甲最终耐久度 = 单位耐久度 × 部位倍率。例如钻石胸甲 = 33 × 16 = 528。

---

## 9. 步骤 5：物品附魔值

### 来源

附魔值通过 `javap -c -p` 自动从客户端 jar 中的 class 文件提取。

| 来源文件 | 提取方法 | 条目数 |
|---------|---------|:------:|
| `ToolMaterial.class` | 解析构造器参数的 2nd int（`enchantmentValue`） | 7 材料 → 35 物品 |
| `ArmorMaterials.class` | 解析 `makeDefense` 后的 1st int | 8 材料 → 33 物品 |
| `Items.class` | 匹配 `.enchantable(N)` 调用 + 前序 int push | 6 特殊物品 |

> **不再硬编码。** 脚本动态解析 3 个 class 文件，任何版本更新只需重新运行即可。

提取逻辑见 `_parse_tool_materials_javap()`、`_parse_armor_materials_javap()`、
`_parse_items_enchantability_javap()` 和 `load_enchantability_from_source()`。

### 映射表

**工具材料**（`ToolMaterial.java:23-29`）：

| 材料 | 附魔值 | 适用物品 |
|------|:------:|----------|
| WOOD | 15 | `wooden_*` |
| STONE | 5 | `stone_*` |
| COPPER | 13 | `copper_*` |
| IRON | 14 | `iron_*` |
| DIAMOND | 10 | `diamond_*` |
| GOLD | 22 | `golden_*` |
| NETHERITE | 15 | `netherite_*` |

**盔甲材料**（`ArmorMaterials.java:12-38`）：

| 材料 | 附魔值 | 适用物品 |
|------|:------:|----------|
| LEATHER | 15 | `leather_*` |
| COPPER | 8 | `copper_*` |
| CHAINMAIL | 12 | `chainmail_*` |
| IRON | 9 | `iron_*` |
| GOLD | 25 | `golden_*` |
| DIAMOND | 10 | `diamond_*` |
| TURTLE_SCUTE | 9 | `turtle_helmet` |
| NETHERITE | 15 | `netherite_*` |

**特殊物品**（`Items.java` 显式 `.enchantable(N)` 调用）：

| 物品 | 附魔值 | 行号 |
|------|:------:|:----:|
| `bow` | 1 | 1175 |
| `crossbow` | 1 | 2044 |
| `trident` | 1 | 2029 |
| `fishing_rod` | 1 | 1490 |
| `book` | 1 | 1426 |
| `mace` | 15 | 1697 |

**无附魔值的物品**（不可附魔台附魔）：`elytra`、`shears`、`shield`、所有马铠、`wolf_armor`。

---

## 10. 步骤 6：Limited_Level 计算

### 背景

MC 1.21+ 的 data-driven 系统中**不存在独立的 `limited_level` 字段**。
只有一个 `max_level`。附魔台和铁砧读取同一个值。

实际等级限制由**成本系统**动态产生：

```
附魔台：从 max_level 向下遍历，检查  power ≥ minCost(level)
铁砧：  直接允许到 max_level
```

### 计算公式

**物品最大能量**（`_max_power`）：

```python
base = 30                     # 15书架，第3格
added = 1 + 2 × (附魔值 // 4)  # 附魔值随机加成上限
max_power = round((30 + added) × 1.15)  # 随机浮动上限
```

参考 `EnchantmentHelper.java`：
- `getEnchantmentCost()`（行 494-510）：基础成本
- `selectEnchantment()`（行 540-571）：物品附魔值修正

**等级成本检查**（`_min_cost`）：

```python
minCost(level) = min_cost.base + min_cost.per_level_above_first × (level - 1)
```

参考 `EnchantmentHelper.java` 行 587-601 的用 `getMinCost(level)` 判断。

### 计算流程

```
对于每个魔咒 e:
    best = 0
    对于 e 的每个适用物品 i:
        power = _max_power(i.附魔值)
        从 max_level 向下遍历 level:
            如果 min_cost(level) ≤ power:
                best = max(best, level)
                跳出内层循环
    e.limited_level = max(1, best)
```

### 判定示例

| 魔咒 | max_level | minCost(最高级) | 适用物品最高附魔值 | 最大能量 | limited_level |
|------|:---------:|:---------------:|:-----------------:|:--------:|:------------:|
| Sharpness | 5 | 45 (金剑) | 22 (金) | 47 | **5** |
| Power | 5 | 45 | 1 (弓) | 36 | **4** |
| Quick Charge | 3 | 60 | 1 (弩) | 36 | **2** |
| Thorns | 3 | 40 (金甲) | 25 (金胸甲) | 47 | **3 → 2** |
| Lunge | 3 | ? | 15 (锤) | 43 | **1** |
| Swift Sneak | 3 | 50+ | ? | ? | **1** |

---

## 11. 步骤 7：适用性模型（supported_items ∩ 物品 tag）

### 模型

T10/T11 之后，适用性判定完全对齐真实 MC 的 tag 成员模型，**不再做类别推导**：

- 每个魔咒携带原始 `supported_items`（`#tag` 引用或具体物品 ID），脚本**透传不展开**——
  `#minecraft:enchantable/sharp_weapon` 这样的引用原样写入 vanilla.json。
- 装备的 `category` 只是**显示短名**（如 `"sword"`），从物品分组 tag / ID 后缀派生，
  仅供显示与导出，**不参与适用性判定**。
- 装备数据本身从 `enchantable/*` 物品 tag 成员派生物品列表（见第 8 节）。

### 适用性判定

```
enchantment 适用于 item  ⟺  supported_items ∩ tags_of(item) ≠ ∅
```

`tags_of(item)` 由 `TagResolver` 反查 item 所属的全部 `#tag`（含 `enchantable/*`、
`minecraft:swords` 等物品 tag，嵌套 tag 惰性 BFS 展开）。业务→算法边界
（`CompactAdapter::is_supported`）用该谓词过滤目标装备可用的魔咒。

### vanilla fallback 与交叉验证

- vanilla profile 是**内置根**（`builtin:vanilla`）；自定义 profile 加载时以 vanilla
  tag/装备全宇宙为基准（T7 两阶段加载），profile 自身的 DTO 在并集上做**惰性交叉验证**：
  `#tag` 引用需在 tag 定义中存在、具体物品 ID 需在装备注册表中存在，否则**丢弃**；
  引用全部丢弃则整条魔咒被跳过（T6）。
- `#tag` 引用在解析期不展开、在加载期也不二次包裹——单解析路径，避免旧的"双层 tag 解析"冲突。

---

## 12. 语言资源提取

### 12.1 来源

Minecraft 语言文件通过 Mojang 资源服务器获取，无需下载完整客户端 jar：

```
version_manifest_v2.json
         ↓
  版本详情 JSON  ──→  assetIndex.url
         ↓
  Asset Index JSON  ──→  objects["minecraft/lang/<locale>.json"].hash
         ↓
  resources.download.minecraft.net/<hash[:2]>/<hash>
         ↓
  语言 JSON 文件
```

### 12.2 提取方法

`scripts/vanilla/lang.py` 中的 `run()` 函数：

1. 查询 `version_manifest_v2.json` 获取最新版本
2. 下载 Asset Index JSON（包含所有资源文件的哈希索引）
3. 查找 `minecraft/lang/<locale>.json` 条目
4. 通过哈希值构造下载 URL 并下载
5. 过滤出与项目相关的翻译键
6. 输出到 `data/i18n/minecraft/<locale>.json`

### 12.3 键过滤

从原始语言文件中保留以下前缀的键：

| 前缀 | 用途 | 示例 |
|------|------|------|
| `enchantment.minecraft.` | 魔咒显示名称 | `enchantment.minecraft.sharpness` → `"Sharpness"` / `"锋利"` |
| `item.minecraft.` | 物品/装备名称 | `item.minecraft.diamond_sword` → `"Diamond Sword"` / `"钻石剑"` |

### 12.4 多语言支持

```bash
# 默认 en_us + zh_cn
python scripts/vanilla/cli.py --lang-only

# 指定语言
python scripts/vanilla/cli.py --lang-only --locales zh_cn,en_us,ja_jp

# 下载全部 142+ 种语言
python scripts/vanilla/cli.py --lang-only --locales all
```

### 12.5 回退机制

当 locale 不在 Asset Index 中时（如 `en_us` 作为基础语言内置于游戏），
自动回退到已提取的客户端 jar 中的语言文件。

---

## 13. 输出格式

### 13.1 vanilla.json

```json
{
  "name": "Vanilla",
  "description": "Built-in vanilla Minecraft data pack (Java Edition {version})",
  "author": "BestEnchSeq",
  "version": "2.0.0",
  "enchantments": [
    {
      "id": "sharpness",
      "name": "Sharpness",
      "platform": "java",
      "max_level": 5,
      "limited_level": 5,
      "multiplier": 1,
      "exclusive_set": ["breach", "density", "impaling", "smite"],
      "supported_items": [
        "#minecraft:enchantable/weapon"
      ]
    }
  ],
  "equipments": [
    {
      "id": "diamond_sword",
      "name": "Diamond Sword",
      "category": "sword",
      "max_durability": 1561
    }
  ],
  "tags": {
    "minecraft:enchantment/non_treasure": {
      "values": ["minecraft:sharpness", ...]
    }
  }
}
```

### 13.2 minecraft i18n 文件

`data/i18n/minecraft/<locale>.json`：

```json
{
  "language": "zh_CN",
  "minecraft_locale": "zh_cn",
  "source_version": "26.2",
  "strings": {
    "enchantment.minecraft.sharpness": "锋利",
    "item.minecraft.diamond_sword": "钻石剑",
    ...
  }
}
```

### 容量

| 文件 | 大小 |
|------|:----:|
| `data/builtin/vanilla.json` | ~33 KB |
| `data/i18n/minecraft/en_US.json` | ~49 KB |
| `data/i18n/minecraft/zh_CN.json` | ~49 KB |

---

## 14. C++ 侧解析与 i18n

### 14.1 注册表数据加载

C++ 代码通过 `besq::data::load_builtin_data()` 从 `vanilla.json` 加载注册表：

```
FormatDetector::parse(path) 或 NativeJsonParser::parse(json)
  → EnchantmentData / EquipmentData DTO
  → RegistryLoader::from_dto()  (两阶段：vanilla 基准 + profile 交叉验证)
  → EnchantmentRegistry / EquipmentRegistry / TagRegistry
```

| JSON 字段 | EnchInfo 字段 | 说明 |
|-----------|-------------|------|
| `id` | `id` (string → NSID) | 唯一标识 |
| `name` | `display_name` → `name` | 显示名称（仅用于数据快照，运行时由 i18n 重写） |
| `max_level` | `max_level` | 最大等级 |
| `limited_level` | `limited_level` | 0 或缺失时 = max_level |
| `multiplier` | `multiplier` | 费用倍率 |
| `exclusive_set` | `exclusive_with` → `exclusive_set` | 冲突魔咒 |
| `supported_items` | `applicable_to` → `supported_items`（透传不展开） | 适用物品（`#tag` 或具体 ID） |

### 14.2 国际化（i18n）系统

实体显示名称不再从 `vanilla.json` 的 `name` 字段读取，而是通过 NSID + Language 系统推导：

```
NSID "minecraft:sharpness"
  → NSID::str(Callable) → "enchantment.minecraft.sharpness"  (翻译键)
  → Language::get(key)  → "锋利" / "Sharpness"               (本地化名)
  → 兜底: id.str()     → "minecraft:sharpness"               (功能无损)
```

**关键组件：**

| 文件 | 说明 |
|------|------|
| `src/common/i18n/NsidDisplay.h` | `ench_display_name()` / `item_display_name()` — NSID 推导 + `tr()` 查询 |
| `src/common/i18n/Language.h` | `Language` / `LanguageManager` — 翻译表 + 多语言切换 |
| `src/builtin/I18nLoader.cpp` | 启动时注册 UI 翻译 + Minecraft 实体名称到同一 Language 实例 |

**数据流：**

```
编译时嵌入（EmbedResource.cmake）
  data/i18n/en_US.json          → i18n_en_US()      (UI 字符串)
  data/i18n/zh_CN.json          → i18n_zh_CN()      (UI 字符串)
  data/i18n/minecraft/en_US.json → mc_i18n_en_US()  (Minecraft 实体名称)
  data/i18n/minecraft/zh_CN.json → mc_i18n_zh_CN()  (Minecraft 实体名称)

启动时合并（I18nLoader::register_builtin_translations）
  "zh_CN" = UI_zh_CN.merge(MC_zh_CN)
  "en_US" = UI_en_US.merge(MC_en_US)

运行时查询
  tr("enchantment.minecraft.sharpness")
  → 已注册 → "锋利" / "Sharpness"
  → 未注册 → 返回 "enchantment.minecraft.sharpness" 或兜底 id.str()
```

**`--verbose` 模式：**
默认只显示翻译名（`锋利 V`），`--verbose` 时同时显示 NSID（`锋利 (minecraft:sharpness) V`）。

**动态语言加载：**
程序从可执行文件旁的 `langs/<code>.json` 目录按需加载语言文件（通过 `LanguageManager::set_langs_dir()`），
同 key 时自动合并到已有翻译。参见 `src/main.cpp`。

### 14.3 输出层适配

OutputFormatter 不再使用 `EnchInfo::name` 或 `NSID::str()`，而是统一通过 `NsidDisplay.h` 中的函数：

| 输出格式 | 物品名 | 魔咒名 |
|---------|--------|--------|
| verbose/text | `item_display_name(item.id)` | `ench_display_name(ench.id)` |
| compact | `item.id.str()`（NSID 字符串，机器可读） | `ench.id.str()` |
| JSON | `item.id.str()`（id 字段，机器可读） | `ench.id.str()` |

### 14.4 Datapack 加载为 Profile（runtime）

besq 支持把真实 MC 数据包（datapack）直接加载为一个 profile，通过 `McOfficialParser` 解析其内部 `data/<ns>/` 目录（真实 MC 1.21+ 格式：`supported_items`/`exclusive_set` 单字符串或数组、`slots`、`tag replace`、`anvil_cost`）。

**pack.mcmeta 检测**：目录含 `pack.mcmeta` 才被识别为 datapack。`ProfileManager::load_datapack(dir)` 显式加载；`ProfileManager::load_directory(dir)` 会把扫描目录下含 `pack.mcmeta` 的子目录当作 datapack 加载（目录中的 `.json`/`.csv` 文件按 native 格式加载）。

**目录布局**：

```
<profiles_dir>/
├── custom.json                      ← native JSON profile
├── my_sheet.csv                     ← native CSV profile
└── <My Pack>/                      ← datapack 子目录（含 pack.mcmeta）
    ├── pack.mcmeta
    └── data/
        ├── <ns1>/                   ← 命名空间 1（如自定义模组）
        │   ├── enchantment/*.json
        │   └── tags/item/*.json
        ├── <ns2>/                   ← 命名空间 2
        │   └── enchantment/*.json
        └── minecraft/               ← 覆盖 vanilla 的命名空间
            └── tags/item/*.json
```

**命名规则**（B-T13：profile key 是字符串，verbatim 保留）：
- profile key = `pack.mcmeta` 中 `pack.id`；无 `pack.id` 时 = 文件夹名，**verbatim 保留**（可含空格/点，不做 NSID 清洗）。
- 根 key 固定为 `builtin:vanilla`；datapack 命名为 `builtin:vanilla`/`vanilla`（旧别名）时改写为 `vanilla_datapack`，防止覆盖内置根。
- datapack 内魔咒/装备/tag id 仍是 NSID（`<ns>:<id>`），如 `mytest:leeching`。

**多命名空间聚合**：一个 datapack = **一个 profile**，其下所有 `data/<ns>/` 命名空间（含覆盖 vanilla 的 `data/minecraft/`）全部聚合进这一个 profile。加载经与 `ProfileLoader` 相同的**两阶段 `RegistryLoader` 路径**（先以内置 vanilla 全宇宙为基准，再对 datapack 自身 DTO 交叉验证），仅保留 datapack 自身内容；vanilla tag 宇宙保留，使 `#minecraft:swords` 等 `#tag` supported_items 引用在业务→算法边界仍可解析。datapack 自动注入隐式依赖 `builtin:vanilla`（写入依赖链，供 `cross_validate` 使用）。

**扫描与 CLI**：默认扫描 `<cwd>/profiles/`（`BesqContext::set_profiles_dir(dir)` 可覆盖，CLI 用 `--profile-dir <dir>`）。`--profile <key>` 激活任意字符串 key 的 profile；`--publish <key> [--publish-version <v> --publish-tag <t>]` 将有效视图拍平为自包含 JSON。

---

## 15. 条目级数据来源参考

### 魔咒数据字段

| 字段 | 来源文件 | 来源字段/方法 | 可靠性 |
|------|---------|--------------|--------|
| `id` | `data/<ns>/enchantment/<id>.json` | 文件名 | 高 |
| `name` | `assets/<ns>/lang/en_us.json` → Language 系统 | `description.translate` 键查表 | 高（运行时从 Language 重写） |
| `platform` | — | 硬编码 `"java"` | 高 |
| `max_level` | 魔咒 JSON | `max_level` | 高 |
| `limited_level` | — | **成本模拟计算** | 中（保守估算） |
| `multiplier` | 魔咒 JSON | `anvil_cost` | 高 |
| `exclusive_set` | 魔咒 JSON | `exclusive_set` | 高 |
| `supported_items` | 魔咒 JSON | `supported_items`（`#tag` 引用透传） | 见第 11 节 | 高 |

### 装备数据字段

| 字段 | 来源 | 方法 |
|------|------|------|
| `id` | `enchantable/*` 标签成员物品 | 标签展开 |
| `name` | `en_us.json` → Language 系统 | `item.minecraft.<id>` 查表（运行时重写） |
| `category` | 物品分组标签 / ID 后缀启发式 | 显示短名，见第 11 节 |
| `max_durability` | `Items.class` 字节码 + 材料常量 | 见第 7 节 |

---

## 附录 A：更新数据

### 全量更新

```bash
# 确保有 JDK（javap 用于字节码分析）
# Java 21+ 推荐

# 删除旧 jar 强制重新下载
rm res/vanilla.jar

# 运行完整提取（ench + lang）
python scripts/vanilla/cli.py --with-lang zh_cn,en_us
```

### 仅语言文件

```bash
# 独立提取语言文件（无需 jar）
python scripts/vanilla/cli.py --lang-only

# 或通过薄封装入口
python scripts/download_mc_lang.py --locales zh_cn,en_us
```

### 仅魔咒/装备数据

```bash
python scripts/vanilla/cli.py              # 等同 python scripts/get_vanilla_data.py
python scripts/get_vanilla_data.py         # 薄封装，同上
```

## 附录 B：Java 源码参考位置

> 以下路径对应客户端 jar 解压后的 class 文件位置（`res/vanilla/`），
> 以及开源版本（MC 1.21+ 官方提供混淆映射表）的源码路径。
> 不需要完整的反编译源代码——关键常量已硬编码到提取脚本中。
> 更新版本时，可用 `javap` 从 `.class` 文件中核对关键数值。

| 类 | 路径（`res/vanilla/` 下） | 用途 |
|----|--------------------------|------|
| `Enchantment` (record) | `net/minecraft/world/item/enchantment/Enchantment.java` 或 `.class` | 魔咒 record 定义 |
| `EnchantmentDefinition` | 同上（嵌套 record） | 魔咒数值（`max_level`, `anvil_cost`） |
| `EnchantmentHelper` | 同级目录 | 附魔台成本计算公式 |
| `ToolMaterial` | `net/minecraft/world/item/ToolMaterial.java` 或 `.class` | 工具材料附魔值（已硬编码） |
| `ArmorMaterials` | `net/minecraft/world/item/equipment/ArmorMaterials.class` | 盔甲材料附魔值（已硬编码） |
| `Items` | `net/minecraft/world/item/Items.class` | 物品注册字节码（`javap` 自动解析） |
| `Enchantable` (record) | `net/minecraft/world/item/enchantment/Enchantable.class` | 附魔值数据组件（仅含一个 int） |

## 附录 C：常见问题

### Q: limited_level 是否 100% 精确？

不是。计算基于**最优随机条件**（15 个书架、第 3 格、最优随机数）。在非最优条件下
实际可达等级可能更低。目前的设计目标是给出一个**保守的上界**——它回答了"在最好条件下
附魔台能到多少级"的问题，而不是"实际游戏中平均能到多少级"。

### Q: 为什么有的魔咒在附魔台中不可见但仍然有 limited_level？

`limited_level` 只计算**成本是否允许**，不检查魔咒是否出现在附魔台的可用列表中
（后者由 `#minecraft:in_enchanting_table` / `#minecraft:non_treasure` 标签控制）。
宝藏魔咒（如 Swift Sneak）在附魔台中不会出现，但如果它们出现，计算出的 limited_level
表示其最高可达等级。

### Q: 装备类别推导会出错吗？

不会影响正确性。装备 `category` 只是**显示短名**，**不参与适用性判定**。适用性完全由真实 MC
物品 tag（`enchantable/*` 等）决定，因此模组物品即使命名不规范，只要其 tag 定义正确，
适用性判定就不会出错（见第 11 节）。

### Q: 实体显示名从哪里来？

实体显示名（魔咒名、物品名）不再存储在业务对象的 `name` 字段中。
`Ench::name` 已移除。所有显示名通过 `NSID::str(Callable)` 构建翻译键，
经 `Language` 系统从嵌入的 Minecraft 翻译数据中查询。无翻译时回退到 `NSID::str()`。
参见第 14.2 节。
