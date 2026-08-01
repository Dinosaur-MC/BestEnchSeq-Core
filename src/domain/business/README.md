# 业务域（`src/domain/business/`）

自包含的核心域，以 **Profile** 为操作的一等公民。仅依赖 `common/`。

## 目录结构

```
types/          ← 值类型（Ench, EnchInfo, EnchSet, Item, Equipment, etc.）
  dto/          ← 数据传输对象（EnchantmentData, EquipmentData）
registries/     ← 纯数据容器（EnchantmentRegistry, EquipmentRegistry, TagRegistry）
parsers/        ← 文件格式 → DTO（NativeJsonParser, NativeCsvParser, McOfficialParser）
loaders/        ← DTO ↔ Registry/Profile（RegistryLoader, ProfileLoader）
ProfileManager.h ← 生命周期（Profile 管理，业务域顶层）
components/     ← FormatDetector, Serializer, TagResolver, RegistryHelper（集合运算）
```

## 关键设计

- **Profile 是一等公民**：同时持有 EnchantmentRegistry + EquipmentRegistry + TagRegistry
- **数据流**：File → FormatDetector → Parser → DTO → RegistryLoader → Profile
- **序列化**：类型实现 `IJsonSerializable`，`Serializer.h` 提供 ADL 兼容的自由函数委托

详见 `docs/domain_designs/business-domain-design.md`。
