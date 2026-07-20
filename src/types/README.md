# 类型层（`types/`）

## 架构

类型层定义整个项目使用的数据结构，分为三个层次：

```
RawTypes (string-based, 解析阶段)
    │  RawTypeAdapter::resolve() 将 string ID → int32_t ID
    ▼
Domain types (纯数据容器, 边界 I/O)
    │  CompactAdapter::apply() 转换 domain → compact
    ▼
Compact types (namespace compact, 算法层专用)
```

**依赖关系：** 类型层只依赖于基础 C++ 类型和少量 utils 工具（hash），不依赖任何其他层。

---

## RawTypes（`RawTypes.h`）

解析阶段的字符串中间类型，在 RawTypeAdapter::resolve() 处理前使用。

```cpp
struct RawEnchInfo {
    std::string name_id;
    std::string name;
    MCE supported_platform;          // Java / Bedrock / Both
    int32_t max_level, limited_level;
    int32_t multiplier;
    bool is_treasure;
    std::unordered_set<std::string> exclusive_set;
    std::unordered_set<std::string> applicable_equipment;  // 分类名
};

struct RawEquipment {
    std::string name_id, name, category;
    int32_t max_durability;
};
```

设计目的：**解析器不依赖注册表**。解析器只生成字符串引用，所有 ID 解析在后续的 RawTypeAdapter::resolve() 中统一完成。

---

## Domain types（纯数据）

用于边界 I/O（解析器输入 / 格式化器输出），不包含附魔计算逻辑。

| 类型 | 文件 | 说明 |
|---|---|---|
| `Ench` | `Ench.h/.cpp` | ID + level 对 |
| `EnchSet` | `EnchSet.h/.cpp` | 有序 Ench 集合，`find_by_id()` O(log n) |
| `EnchInfo` | `EnchInfo.h/.cpp` | 完整附魔定义（乘数、互斥、适用装备） |
| `Equipment` | `Equipment.h/.cpp` | 装备定义（种类、耐久、分类） |
| `ItemStack` | `ItemStack.h/.cpp` | 物品 + 附魔 |
| `EnchSolution` | `EnchSolution.h/.cpp` | 算法解（步骤 + 成本） |

所有 domain type 的附魔操作逻辑（combine、cost 计算等）在 algorithm 层的 ForgeEngine 中实现。

### EnchSet（domain 版本）

```cpp
class EnchSet {
    int16_t find_by_id(int16_t id) const;    // 根据 ID 查找等级
    bool has(const Ench& e) const;
    // 无 combine/update_cache 等方法
};
```

Domain EnchSet 和 compact::EnchSet 是独立实现，前者为 I/O 层设计，后者为算法热路径优化。

---

## Compact types（`namespace compact`）

算法层专用类型，零堆分配，极小的内存布局。

| 类型 | 内存 | 说明 |
|---|---|---|
| `Ench` | 4 B | int16_t id + level |
| `EnchSet` | ≤ 64 B inline | SBO：最多 16 附魔，零堆分配，惰性哈希缓存 |
| `EnchInfo` | — | 乘数 mul/mul_b、最大等级、互斥位掩码 |
| `Item` | ~72 B | type + dur + ppn + EnchSet |
| `EnchStep` | — | base + sacrifice + cost |
| `EnchSolution` | — | 步骤序列 + 总消耗 |

类型别名：

```cpp
using EnchCollection = std::vector<Ench>;
using ItemCollection = std::vector<Item>;
```

### compact::EnchSet 特点

- 最多 16 附魔 inline 存储（64 字节对齐）
- `insert()` 使用 `std::lower_bound` 保持有序
- `hash()` 惰性缓存，insert/clear/sort 自动失效
- 排序保证 EnchSet 是规范状态，与 forge 构造顺序无关
- 通过 `__builtin_memcmp` 实现 O(1) 比较

### compact::EnchInfo

```cpp
struct EnchInfo {
    uint16_t mul;                    // 经验乘数（物品）
    uint16_t mul_b;                  // 经验乘数（书）
    uint16_t max_lvl;
    std::vector<MaskType> exc_mask;  // 互斥位掩码
    bool applicable;

    bool is_conflict(const EnchInfo& other) const noexcept;
};
```

`exc_mask` 是 N 个 `uint64_t` 的向量，每个附魔占 1 bit，`is_conflict()` 是 O(1) 按位与。

---

## 配置类型

### `AlgorithmTypes.h`

```cpp
struct AlgorithmInput {
    ForgeConfig config;
    SearchConfig search;
    compact::ItemCollection items;      // items[0] = 装备, 其余为书
    compact::EnchCollection target;     // 目标附魔
    compact::EnchReg ench_reg;
    int32_t initial_bound = INT32_MAX;  // 预热初值
};

struct AlgorithmOutput {
    std::string algorithm_name, algorithm_version;
    std::chrono::milliseconds computation_time;
    std::vector<compact::EnchSolution> solutions;
    size_t task_id;
    compact::Item final_item;
    bool is_valid;
};
```

### 其它类型

- CLI 配置和解析结果类型（`CLIConfig`、`EnchantmentSpec`、`TargetSpec`）在 `src/cli/cli.h` 中定义
- `Platform.h` — `MCE` 枚举（Java / Bedrock / Both）
- `LogTypes.h` — 日志格式相关类型

### `EquipmentCategory.h`

装备分类 ID 常量（sword / tool / ranged / armor / netherite）。

---

## 开发说明

- Domain type 的字段都是 `int32_t` / `std::string` 等标准类型，便于序列化
- Compact type 使用 `int16_t` / `uint8_t` 等紧凑布局，内存对齐由 `alignas` 保证
- 新增 compact type 时确保所有操作（hash、比较、拷贝）在热路径上足够轻量
- `CompactedTypes.cpp` 中的非内联函数（如 EnchSet 的 insert/find）保持最小实现
