# 内置数据层（`src/builtin/`）

**职责边界**：只负责嵌入资源的**收集**（编译期嵌入）与 **raw data 访问**（`std::string_view`）。不做解析、不做 I/O、不依赖任何项目内模块（仅标准库）——所有解析/加载/注册逻辑归属各自的领域层。

## 组件

| 文件 | 类型 | 职责 |
|------|------|------|
| `EmbeddedData.h` | 手写壳头 | 统一 raw 访问入口（转发到生成头） |
| `FrontendAssets.h` | 手写壳头 | 前端资源语义化入口（转发到同一生成头） |
| `BuiltinCore.cpp` | 手写占位 TU | besq-core 聚合库的锚点（CMake 要求 target 非空） |
| `EmbeddedResources_generated.h` | **自动生成** | `enum class ResourceId`（data_/frontend_ 前缀防撞）+ inline `raw()` / `resource_name()` / `group_of()` |
| `<group>_assets.cpp`（data/frontend） | **自动生成** | 每组 constexpr 字节数组 + `detail::<group>_raw()` 实现，编入 `besq-domain-business` |

## 单一事实源

**所有资源声明集中在 CMakeLists.txt 的 `besq_embed_resources()` 调用**（data 组 + frontend 组）。新增/修改/删除资源只改那里，重新 configure 后枚举、接口、实现自动重生成：

```cmake
besq_embed_resources(
    GROUPS
        data besq-domain-business
    RESOURCES
        data_vanilla_json=${CMAKE_CURRENT_SOURCE_DIR}/data/builtin/vanilla.json
        ...
)
```

生成器（`cmake/EmbedResource.cmake` + `EmbedResource_gen.cmake`）在 configure 期校验：成员名合法性、文件存在性、空文件、**组前缀归属**（成员名必须以所属组名开头）、**全局重复成员**（跨组防撞）——违规即 `FATAL_ERROR`。生成代码的 switch 全覆盖由生成器保证；`-Wswitch` 是第二道保险。

## 接口

```cpp
besq::data::raw(ResourceId)              // 嵌入字节（只读 string_view）
besq::data::resource_name(ResourceId)    // 枚举成员名（调试/日志）
besq::data::group_of(ResourceId)         // 组名（"data"/"frontend"）
```

- `raw()` 是头内 inline 分派器：每个成员路由到所属组的 `detail::<group>_raw()`（out-of-line，定义在组实现 .cpp）。跨组访问返回空 view。
- 实现编入 `besq-domain-business`（与 `BuiltinData` 加载器同库）：CLI 与 GUI 都链接该库，符号唯一、无静态链接陷阱（勿把组实现拆分到不同库——同名 `raw()` 双定义会被链接器静默覆盖，另一组资源读空）。

## 调用方（业务逻辑已迁出）

| 原 builtin 组件 | 去向 |
|---|---|
| `DataLoader`（4 API） | `src/domain/business/loaders/BuiltinData.{h,cpp}` |
| `ItemProperties`（解析） | `src/domain/business/components/ItemProperties.{h,cpp}` |
| `I18nLoader`（解析+注册） | `src/domain/interface/components/BuiltinI18n.{h,cpp}` |
| 前端符号声明（gui/main.cpp 手写 17 个） | 删除 → `raw(FrontendAsset::...)` 统一访问 |
