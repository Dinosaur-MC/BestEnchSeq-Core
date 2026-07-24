# 业务注册表层（`src/domain/business/registries/`）

## 架构

业务域注册表负责附魔、装备、标签等的 NSID 管理和查找。为 orchestration 域提供数据源，再由 orchestration 层的 CompactAdapter 转换为紧凑注册表供算法域使用。

```
EnchantmentRegistry (NSID keyed, 完整 EnchInfo)
EquipmentRegistry (NSID keyed, 装备定义)
EquipmentTagRegistry (标签分类)
       │
       │ CompactAdapter::apply() → 提取适用附魔
       ▼
algorithm::EnchReg (dense int16_t 索引, 扁平冲突矩阵)
```

---

## EnchantmentRegistry

核心注册表，管理所有附魔定义。值语义（可拷贝），支持 NSID 键查找。

```cpp
class EnchantmentRegistry {
    void initialize(const std::vector<EnchInfo>& infos);

    // 查找（NSID keyed）
    const EnchInfo& get(const NSID& id) const;
    const EnchInfo& get(int32_t index) const;
    int32_t index(const NSID& id) const;

    // 运行时修改
    void add_or_replace(const EnchInfo& info);
    bool remove(const NSID& id);

    // 查询
    size_t size() const;
    bool contains(const NSID& id) const;
    bool is_incompatible(const NSID& a, const NSID& b) const;
};
```

### 注意事项

- 旧版 `create_subset()` 已被移除。注册表剪枝现在由 `CompactAdapter::apply()` 直接完成：
  提取适用的 `CompactEnchInfo[]` → 调用 `EnchReg::init()` 构建紧凑注册表
- `to_global_id()` / `to_local_id()` 映射由算法域 `EnchReg` 内部维护

---

## EquipmentRegistry / EquipmentTagRegistry

装备和装备标签的查找表。`EquipmentTagRegistry` 取代了旧的 `EquipmentCategoryRegistry`。

```cpp
class EquipmentRegistry {
    void initialize(const std::vector<Equipment>& equipment);
    const Equipment* find(const NSID& id) const;
    const Equipment& get(const NSID& id) const;
    bool contains(const NSID& id) const;
    size_t size() const;
};

// 标签注册表 — 替代旧的 EquipmentCategoryRegistry
// 使用 EquipmentTag 对象而非 int32_t ID
class EquipmentTagRegistry {
    void initialize(const std::vector<std::pair<EquipmentTag, std::string>>& tags);
    bool contains(const EquipmentTag& tag) const;
    const EquipmentTag& get(std::string_view name) const;
    size_t size() const;
};
```

---

## RegistryManager

数据源管理，负责 registry 的发现、筛选、加载和解析。

```cpp
class RegistryManager {
    void add_builtin();                                    // 注册内建 Vanilla 数据
    void scan_registry_dir(const std::filesystem::path& dir); // 扫描目录
    void load_and_resolve(                                 // 加载 + 解析
        std::optional<std::string> filter,
        EquipmentTagRegistry& cat_reg,
        EquipmentRegistry& eq_reg,
        EnchantmentRegistry& ench_reg
    );
};
```

`RegistryManager` 替换了旧 `load_all_data()`。`--registries` 筛选：
- 无筛选 → 加载全部（失败 WARN+SKIP）
- 有筛选 → 限定名称或路径，全部必须成功

---

## 相关组件

| 组件 | 目录 | 说明 |
|------|------|------|
| `CompactAdapter` | `src/domain/orchestration/components/` | domain ↔ compact 转换边界 |
| `EnchReg` | `src/domain/algorithm/registries/` | 算法域紧凑注册表（O(1) 冲突矩阵） |
| `AlgorithmRegistry` | `src/domain/algorithm/plugin/` | 算法策略工厂（按名创建） |
| `TagResolver` | `src/domain/interface/components/` | `#tag` 标签 BFS 展开工具 |
