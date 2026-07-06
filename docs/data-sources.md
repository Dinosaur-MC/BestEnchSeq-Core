# 数据源与提取方法

> 版本：1.0
> 最后更新：2026-07-07

---

## 目录

1. [概述](#1-概述)
2. [数据流水线](#2-数据流水线)
3. [原始来源：Minecraft 客户端 Jar](#3-原始来源minecraft-客户端-jar)
4. [步骤 1：本地化数据](#4-步骤-1本地化数据)
5. [步骤 2：标签系统](#5-步骤-2标签系统)
6. [步骤 3：魔咒数据提取](#6-步骤-3魔咒数据提取)
7. [步骤 4：装备数据提取](#7-步骤-4装备数据提取)
8. [步骤 5：物品附魔值](#8-步骤-5物品附魔值)
9. [步骤 6：Limited_Level 计算](#9-步骤-6limited_level-计算)
10. [步骤 7：装备类别推导](#10-步骤-7装备类别推导)
11. [输出格式](#11-输出格式)
12. [C++ 侧解析流程](#12-c-侧解析流程)
13. [条目级数据来源参考](#13-条目级数据来源参考)

---

## 1. 概述

`data/builtin/vanilla.json` 是 BestEnchSeq-Core 的内建原版数据文件，包含：

| 数据类别 | 条目数 | 说明 |
|---------|:------:|------|
| **enchantments** | 43 | 全部原版魔咒的注册信息 |
| **equipments** | 77 | 可锻造的装备类型 |
| **tags** | 29 | 魔咒冲突组和物品分类标签 |

此文件由 `scripts/get_vanilla_data.py` 从 Mojang 官方游戏客户端 jar 自动提取生成，
**不需要手动维护**。

---

## 2. 数据流水线

```
Mojang 版本清单 (version_manifest.json)
        │
        ▼
    下载最新版客户端 jar
        │
        ▼
    ZIP 解压到 res/vanilla/
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
    │  8. 物品分组    → 装备类别           │
    └──────────────────────────────────────┘
        │
        ▼
    data/builtin/vanilla.json
```

---

## 3. 原始来源：Minecraft 客户端 Jar

### 3.1 版本获取

```python
VERSION_MANIFEST = "https://launchermeta.mojang.com/mc/game/version_manifest.json"
```

脚本通过 Mojang 官方版本清单获取**最新正式版**（`latest.release`）的下载链接。
下载客户端 jar 后缓存到 `res/vanilla.jar`，避免重复下载。

### 3.2 解压结构

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

### 3.3 MC 1.21+ Data-Driven 格式

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

## 4. 步骤 1：本地化数据

### 来源

`assets/minecraft/lang/en_us.json`

### 方法

在解压目录中递归搜索 `en_us.json` 文件（仅存在一个）。此文件是 Mojang 的
官方英文翻译文件，格式为 `{ "翻译键": "显示文本" }`。

### 用途

将魔咒的技术 ID（如 `sharpness`）映射为显示名称（如 `"Sharpness"`）。

魔咒 JSON 中的 `description.translate` 字段（如 `"enchantment.minecraft.sharpness"`）
作为查找键。

---

## 5. 步骤 2：标签系统

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

## 6. 步骤 3：魔咒数据提取

### 来源

`data/<ns>/enchantment/<id>.json`

### 提取字段

| 字段 | JSON 源 | 说明 |
|------|---------|------|
| `id` | 文件名 | `{ns}:{filename}`，如 `"minecraft:sharpness"` |
| `name` | `description.translate` → 查 `en_us.json` | 显示名称 |
| `platform` | 硬编码 `"java"` | MC 官方为跨平台数据 |
| `max_level` | `max_level` | 铁砧/附魔台统一最大等级 |
| `limited_level` | **计算所得** | 见[第 9 节](#9-步骤-6limited_level-计算) |
| `multiplier` | `anvil_cost` | MC 1.21+ 改名，含义相同 |
| `exclusive_set` | `exclusive_set` / `exclusiveSet` | 冲突魔咒 ID 列表，展开 `#` 标签引用 |
| `applicable_equipment` | `supported_items` → 类别推导 | 见[第 10 节](#10-步骤-7装备类别推导) |

### 兼容性处理

- `exclusive_set` 在 1.21+ 中可能是字符串或数组，两种都处理
- `supported_items` 同理
- `exclusiveSet`（驼峰）作为 `exclusive_set` 的后备键

---

## 7. 步骤 4：装备数据提取

### 来源

从 `enchantable/*` 标签派生物品列表，而非直接读取装备定义 JSON。

MC 1.21+ 没有独立的"装备注册表"。可附魔的物品通过 `enchantable/<category>` 标签
隐式定义。

### 提取步骤

1. 扫描所有名称包含 `/enchantable/` 的标签
2. 展开标签引用，收集所有引用的物品 ID
3. 从 `load_durability_from_source()` 获取耐久度值
4. 过滤掉无耐久度的物品（耐久度 ≤ 0 时跳过）
5. 通过物品 ID 后缀推导装备类别

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

## 8. 步骤 5：物品附魔值

### 来源

附魔值通过分析 Java 源码得到，硬编码为常量表。

> **⚠️ 不依赖运行时反编译。** 此表是通过阅读 Minecraft 开源/反编译源码
> （`ToolMaterial.java`、`ArmorMaterials.java`、`Items.java`）手动整理的常量，
> 在脚本中以 `load_enchantability_from_source()` 硬编码维护。
> 更新 Minecraft 版本时需要人工核对是否有变动。

在 Java 代码中，附魔值存储在物品数据的 `DataComponents.ENCHANTABLE` 组件中，
通过 `Item.Properties.enchantable(int)` 方法设置。

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

## 9. 步骤 6：Limited_Level 计算

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

## 10. 步骤 7：装备类别推导

### 问题

MC 1.21+ 的 `supported_items` 标签引用的是具体物品 ID（如 `minecraft:diamond_sword`），
但 BestEnchSeq 的装备类别系统需要的是类别名称（如 `"sword"`）。

### 推导方法

**方法 A — 物品分组标签（优先）**

1. 扫描 `tags/item/*` 下的所有标签（排除 `enchantable/*`）
2. 对每个标签，解析其包含的物品 ID 列表
3. 提取物品 ID 的**共同后缀**（通过 `Counter` 取众数）
4. 建立 `标签键 → 类别名` 映射

举例：
```
minecraft:foot_armor  → ["minecraft:diamond_boots", "minecraft:iron_boots", ...]
                        → 共同后缀 "boots"
                        → 类别 "boots"
```

**方法 B — ID 后缀回退（当无匹配标签时）**

- `diamond_sword` → 取最后一段 `sword` → 类别 `"sword"`
- `bow` → 无下划线 → 整体 `bow` → 类别 `"bow"`

### 类别约束

魔咒的 `applicable_equipment` 是其所有适用装备类别的并集。

---

## 11. 输出格式

`data/builtin/vanilla.json` 格式：

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
      "applicable_equipment": ["axe", "spear", "sword"]
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

### 容量

当前版本：约 31 KB，43 个魔咒 + 77 个装备 + 29 个标签。

---

## 12. C++ 侧解析流程

C++ 代码通过两个解析入口读取 `vanilla.json`：

### 魔咒解析 — `EnchInfoParser::parse_native_json()`

1. 读取 JSON → 提取 `enchantments` 数组
2. 依次处理内联标签（`tags` 对象）
3. 遍历每个魔咒条目，构造 `EnchInfo` 对象
4. 字段映射：

| JSON 字段 | EnchInfo 字段 | 说明 |
|-----------|--------------|------|
| `id` | `name_id` | 唯一标识 |
| `name` | `name` | 显示名称 |
| `platform` | `supported_platform` | 平台 |
| `max_level` | `max_level` | 最大等级 |
| `limited_level` | `limited_level` | 0 或缺失时 = max_level |
| `multiplier` | `multiplier` | 费用倍率 |
| `exclusive_set` | `exclusive_set` | 冲突魔咒集合 |
| `applicable_equipment` | `applicable_equipment` | EquipmentCategory 集合 |

### 装备解析 — `EquipmentParser::parse_native_json()`

1. 读取 JSON → 提取 `equipments` 数组
2. 遍历每个装备条目
3. 字段映射：

| JSON 字段 | EquipmentType 字段 |
|-----------|-------------------|
| `id` | `id` |
| `name` | `name` |
| `category` | `category` (EquipmentCategory) |
| `max_durability` | `max_durability` |

### 内联标签处理

`process_inline_tags()` 从 JSON 的 `tags` 对象读取标签定义，
通过 `TagResolver.add_tag()` 注册到全局标签解析器中，在后续 `#` 引用展开时使用。

---

## 13. 条目级数据来源参考

### 魔咒数据字段

| 字段 | 来源文件 | 来源字段/方法 | 可靠性 |
|------|---------|--------------|--------|
| `id` | `data/<ns>/enchantment/<id>.json` | 文件名 | 高 |
| `name` | `assets/<ns>/lang/en_us.json` | `description.translate` 键查表 | 高 |
| `platform` | — | 硬编码 `"java"` | 高 |
| `max_level` | 魔咒 JSON | `max_level` | 高 |
| `limited_level` | — | **成本模拟计算** | 中（保守估算） |
| `multiplier` | 魔咒 JSON | `anvil_cost` | 高 |
| `exclusive_set` | 魔咒 JSON | `exclusive_set` | 高 |
| `applicable_equipment` | 物品标签 + 类别推导 | 见第 10 节 | 中（启发式） |

### 装备数据字段

| 字段 | 来源 | 方法 |
|------|------|------|
| `id` | `enchantable/*` 标签成员物品 | 标签展开 |
| `name` | `en_us.json` | `item.minecraft.<id>` 查表 |
| `category` | 物品分组标签 / ID 后缀启发式 | 见第 10 节 |
| `max_durability` | `Items.class` 字节码 + 材料常量 | 见第 7 节 |

---

## 附录 A：更新数据

```bash
# 确保有 JDK（javap 用于字节码分析）
# Java 21+ 推荐

# 删除旧 jar 强制重新下载
rm res/vanilla.jar

# 运行提取脚本
python scripts/get_vanilla_data.py
```

首次运行会自动下载最新客户端 jar（约 200 MB），后续运行使用缓存。

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

对于原版物品，类别推导通常准确。模组物品的非标准命名模式可能导致误分类，
但可以通过自定义数据包中的标签覆盖。
