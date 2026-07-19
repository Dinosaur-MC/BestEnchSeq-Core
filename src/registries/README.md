# 注册表层（`registries/`）

## 架构

注册表负责附魔、装备、算法实现等的 ID 管理和查找。分为两个层次：

```
Domain registries (string / int32_t 查找)
    │  create_subset() 派生出 compact 子集
    ▼
Compact registries (namespace compact, dense 索引)
```

---

## EnchantmentRegistry

核心注册表，管理所有附魔定义。值语义（可拷贝）。

```cpp
class EnchantmentRegistry {
    // 初始化
    void initialize(const std::vector<EnchInfo>& infos);
    void reset_for_testing();

    // 子集派生（核心功能）
    EnchantmentRegistry create_subset(std::span<const int32_t> global_ids) const;

    // ID 映射
    int32_t to_global_id(int32_t local_id) const;   // 子集 → 全局
    int32_t to_local_id(int32_t global_id) const;   // 全局 → 子集
    bool is_subset() const;                          // 是否为子集

    // 查找
    const EnchInfo& get(int32_t index) const;        // O(1) 按 index
    const EnchInfo& get(std::string_view name_id) const; // 按名称
    int32_t get_id(std::string_view name_id) const;  // 名称 → index

    // 查询
    size_t size() const;
    const auto& get_instances() const;                // 所有 EnchInfo
    const auto& get_exclusive_set(int32_t e) const;   // 互斥附魔集合
    bool is_incompatible(int32_t e1, int32_t e2) const;

    // 验证
    static bool check_validation(const std::vector<EnchInfo>& infos);
};
```

### create_subset()

这是注册表的核心设计。对于每个具体的搜索问题（目标装备 + 可用附魔），**创建一个只包含适用附魔的稠密子集**。子集内 index 从 0 开始连续排列，算法层使用这些 local ID 而非全局 ID，从而：
- 减少 compact 注册表的体积（N×N 冲突矩阵）
- 提高缓存局部性
- 使 compact::Ench 的 id 字段在算法热路径上更小

```cpp
EnchantmentRegistry full_reg;
// ... 初始化 ...
auto subset = full_reg.create_subset(applicable_ids);
// subset 内: id 0..K-1 连续, to_global_id(0) = 原始全局 ID
```

---

## EquipmentRegistry / EquipmentCategoryRegistry

装备和装备分类的查找表。

```cpp
class EquipmentRegistry {
    void initialize(const std::vector<Equipment>& equipment);
    const Equipment* find(std::string_view name_id) const;
    bool contains(std::string_view name_id) const;
    size_t size() const;
};

class EquipmentCategoryRegistry {
    int32_t get_or_create(std::string_view name);   // 查找或创建分类 ID
    std::string_view get_name(int32_t id) const;
    bool contains(std::string_view name) const;
    size_t size() const;
};
```

`EquipmentRegistry` 用于解析用户输入的目标装备。
`EquipmentCategoryRegistry` 用于将字符串分类名（如 `"sword"`）映射为 int32_t ID，在附魔互斥矩阵和适用性检查中使用。

---

## CompactedRegistries（`namespace compact`）

算法层使用的紧凑注册表，local 实例（非单例）。

```cpp
namespace compact {

class EnchReg {
    // 针对目标装备初始化（预计算冲突矩阵 + 附魔信息表）
    void init(const EnchantmentRegistry& registry,
              const Equipment& target_equip);

    // 查询
    size_t size() const noexcept;
    const EnchInfo& get(int32_t id) const;               // 带边界检查
    const EnchInfo& operator[](int32_t id) const noexcept; // 无边界检查（热路径）
    bool is_conflict(int32_t id1, int32_t id2) const noexcept; // O(1) 矩阵查询

    // ID 映射
    int32_t to_local_id(int32_t global_id) const;
    const EnchantmentRegistry& get_registry() const;
    const EnchInfo& get_rich(int32_t id) const;           // 完整的 EnchInfo

    const Equipment& get_target_equip() const noexcept;
};
```

} // namespace compact
```

### init() 流程

1. 从 `EnchantmentRegistry` 拷贝适用附魔的 `EnchInfo`（mul、max_lvl、applicable 等）
2. 预计算 N×N 冲突矩阵（`vector<char>`），`is_conflict()` 为 O(1) 数组访问
3. 存储目标装备引用，后续适用性检查不再需要 EquipmentRegistry

预计算后的 `EnchReg` 可在算法热路径上零分配使用。

---

## RegistryManager

数据源管理，负责 registry 的发现、筛选、加载和解析。替换了原先的 `load_all_data()` 函数。

```cpp
class RegistryManager {
    // 注册内建数据（嵌入式 vanilla.json，name="Vanilla"）
    void add_builtin();

    // 扫描目录下的所有合法 registry 文件/子目录
    void scan_registry_dir(const std::filesystem::path& dir);

    // 加载已发现/筛选的 registry 并解析到 domain registries
    // filter: nullopt → 加载全部（失败 WARN+SKIP）
    //         非空   → 逗号分隔的名称或路径（全部必须成功）
    void load_and_resolve(
        std::optional<std::string> filter,
        EquipmentCategoryRegistry& cat_reg,
        EquipmentRegistry& eq_reg,
        EnchantmentRegistry& ench_reg
    );
};
```

### 在 main.cpp 中的用法

```cpp
RegistryManager mgr;
mgr.add_builtin();
if (config.registry_dir)
    mgr.scan_registry_dir(*config.registry_dir);
mgr.load_and_resolve(config.registries, cat_reg, eq_reg, ench_reg);
```

### 筛选模式

- **无筛选**（`--registries` 未设置）：加载所有已发现的 registry，加载失败时仅 WARN 并跳过
- **有筛选**（`--registries` 已设置）：只加载匹配的 registry，全部必须成功，否则报错

`--registries` 的每个值可以是 registry 名称（匹配 metadata.name）或文件/目录路径（直接加载）。
路径优先识别：如果值在磁盘上存在文件/目录，则按路径加载。

---

## AlgorithmRegistry

算法工厂。策略模式 — 通过名称查找并实例化算法。

```cpp
class AlgorithmRegistry {
    // 注册算法
    template <typename Algo>
    void register_algorithm();

    // 工厂
    std::unique_ptr<IAlgorithm> create(std::string_view name) const;

    // 查询
    bool contains(std::string_view name) const;
    std::vector<std::string> available_algorithms() const;
    size_t size() const;
};
```

所有内置算法在 `main.cpp` 的 `register_builtin_algorithms()` 中注册：

```cpp
registry.register_algorithm<GreedyAlgorithm>();
registry.register_algorithm<DFSAlgorithm>();
registry.register_algorithm<AStarAlgorithm>();
registry.register_algorithm<IDAStarAlgorithm>();
registry.register_algorithm<HierarchicalMergeAlgorithm>();
registry.register_algorithm<DynamicPenaltyBalancingAlgorithm>();
// ...
```

---

## RegistryAccess

Meyer's 单例访问器，提供全局注册表实例。

```cpp
namespace registries {
    EnchantmentRegistry&       enchantment();
    EquipmentRegistry&         equipment();
    EquipmentCategoryRegistry& equipment_category();
    AlgorithmRegistry&         algorithm();
}
```

初始化顺序在程序启动时由 `registries::initialize()` 控制。

---

## TagResolver（`TagResolver.hpp`）

`#tag` 引用解析工具。

```cpp
class TagResolver {
    static std::unordered_set<std::string> resolve(
        const std::string& tag,
        const json::Json& data);
};
```

将 JSON 数据中的 `#minecraft:swords` 等标签引用展开为实际的物品/附魔列表。

---

## 性能说明

| 操作 | 复杂度 | 说明 |
|---|---|---|
| `EnchReg::is_conflict()` | O(1) | 预计算 N×N 矩阵 |
| `EnchReg::operator[]` | O(1) | dense 索引，无边界检查 |
| `EnchReg::init()` | O(N²) | N = 适用附魔数，仅在算法开始时执行一次 |
| `create_subset()` | O(global_size) | 拷贝 + 重映射 |
| `AlgorithmRegistry::create()` | O(n) | n = 已注册算法数 |
