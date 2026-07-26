# 内置数据层（`src/builtin/`）

项目级内置数据工具，由 `ProfileLoader::load_builtin()` 调用。

## 组件

| 文件 | 职责 |
|------|------|
| `DataLoader.h/.cpp` | 读取编译时嵌入的 vanilla.json → 输出 DTO 流 |
| `EmbeddedData.h` | 声明由 CMake `EmbedResource.cmake` 嵌入的二进制资源 |
| `ItemProperties.h/.cpp` | 原版物品属性定义（耐久度、最大合并等级等） |

## 数据流

```
vanilla.json (编译时嵌入)
  → EmbeddedData (CMake EmbedResource)
  → DataLoader::load() → EnchantmentData[] + EquipmentData[]
  → ProfileLoader::load_builtin() → Profile
```
