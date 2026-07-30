# 算法策略插件（`plugins/`）

本目录存放作为独立共享库构建的外部算法策略，在运行时由主程序动态加载（dlopen/LoadLibrary）。

## 可用插件

| 策略 | 目录 | 类型 | 复杂度 | 方法 |
|------|------|------|--------|------|
| Greedy | `greedy/` | 确定性 | O(n²) | 每次选预估成本最低的 pair |
| DiffFirst | `diff_first/` | 确定性 | O(n²) | PPN 分层，每层选最便宜的 |
| HierarchicalMerge | `hierarchical/` | 确定性 | O(n²) | 分组合并，递归 |
| DynamicPenaltyBalance | `penalty_balance/` | 确定性 | O(n²) | 动态平衡惩罚成本 |
| IDA* | `idastar/` | 搜索 | — | 迭代加深 + TT best_g 剪枝 |

### 确定性算法

确定性算法**不展开搜索树**，通过固定策略合并物品。速度快但解的质量不可控。

### IDA* 搜索

IDA*（Iterative Deepening A*）是搜索算法，可在合理时间内找到更优解。使用 `TTTable` 进行 transposition table 剪枝。

## 构建

插件是独立的 CMake 项目，需要预先构建好的 `besq-core` 库。

```bash
# 1. 构建主程序
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 2. 构建插件（必须使用与主程序相同的工具链）
cmake -S plugins -B build/plugins -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/plugins
```

输出文件位于 `build/plugins/algo_<name>.dll`（或 `.so`）。

### 工具链要求

插件**必须使用与主程序完全相同的编译器**，否则运行时加载会因 ABI 不兼容而失败。本项目默认使用 LLVM/Clang 工具链。

### CMake 选项

| 选项 | 说明 |
|------|------|
| `BESQ_CORE_DIR` | 指向主程序构建目录（含 `besq-coreConfig.cmake`）的路径 |

## 运行时加载

使用 `--algo-dir` 参数或 `BESQ_ALGO_DIR` 环境变量指定插件目录：

```bash
# CLI 参数
./build/bin/besq --algo-dir build/plugins --algorithm greedy \
  --target diamond_sword --source "sharpness=5"

# 环境变量
export BESQ_ALGO_DIR=build/plugins
./build/bin/besq --algorithm idastar --target ...
```

列出所有可用策略（含内置 + 插件）：

```bash
./build/bin/besq --algo-dir build/plugins --list-algorithms
```

## 插件 C ABI

每个插件导出一个 C 符号（无 name mangling），返回一个 `IAlgorithm*` 实例：

```cpp
extern "C" void* besq_create_algorithm();
```

主机通过 `GetProcAddress` / `dlsym` 查找此符号并调用工厂函数。主机与插件共享 `besq-core` 库的 vtable 和堆，因此无需特殊的销毁函数。

### 安全审计（自动执行）

从 `besq-core` 开始，每个插件加载时会自动执行安全审查：

1. **W^X 检查** — 检测是否有同时可写和可执行的内存段（拒绝加载）
2. **导入检查** — 检测是否导入了 `socket`、`fork`、`dlopen` 等危险符号
3. **导出检查** — 列举除标准入口外的额外导出符号
4. **链接库审计** — 列出所有 DT_NEEDED / Import DLL
5. **Capability Manifest** — 验证插件声明的权限等级

### 便捷宏

在 `plugin.cpp` 中使用 `BESQ_PLUGIN_ENTRY` 宏即可自动生成导出符号和权限声明：

```cpp
#include "domain/algorithm/plugin/PluginEntry.h"
#include "<name>/<Name>Algorithm.h"

// 默认声明为 PluginCapability::None（纯计算，推荐）
BESQ_PLUGIN_ENTRY(NameAlgorithm)

// 或显式指定权限等级：
// BESQ_PLUGIN_ENTRY_CAP(NameAlgorithm, PluginCapability::None)
```

## 创建新插件

### 开发清单

- [ ] 在 `plugins/<name>/` 下创建 `NameAlgorithm.h`、`NameAlgorithm.cpp`、`plugin.cpp`
- [ ] 继承 `IAlgorithm`，实现 `name()` / `version()` / `execute()`
- [ ] `plugin.cpp` 使用 `BESQ_PLUGIN_ENTRY(NameAlgorithm)` 导出工厂符号
- [ ] `#include` 路径以 `plugins/` 为根，如 `#include "name/NameAlgorithm.h"`
- [ ] 链接 `besq-core`（已有 CMakeLists.txt 自动处理）
- [ ] 构建并测试：`cmake --build build/plugins`
- [ ] 将插件目录添加到 `build/plugins`，通过 `--algo-dir` 加载验证

### 注意事项

- 插件中不能使用主程序的编译定义（如 `BESQ_DISABLE_DIAGNOSTICS`），只能依赖 `besq-core` 导出的 API
- 插件的 `name()` 返回值不能与已有策略（内置或其他插件）重复，否则后加载的会覆盖先加载的
- 如需新增插件测试，在插件目录内创建独立的测试文件，通过 `add_test()` 集成到插件项目的 CMakeLists 中

## 算法开发规范

### EnchSet 访问

**优先使用非迭代器 API**。详见 `src/domain/algorithm/README.md` → "EnchSet 访问规范"。

```cpp
// ✅ 推荐
ench_set.contains(id);              // 存在性检查
ench_set[id];                       // 取等级
ench_set.first() / .next(id);       // 顺序遍历

// ✅ 位运算
auto same = target.enchs & sacrifice.enchs;         // 交集
auto diff = sacrifice.enchs - target.enchs;         // 差集

// ✅ bit_iterator 遍历
bit_iterator<EnchSet::mask_type, uint8_t> it(diff);
for (auto i = it.next(); i != it.npos; i = it.next()) {
    auto level = sacrifice.enchs[i];
    // ...
}
```

### ForgeEngine

`IForgeEngine` 是虚拟接口。`forge_into()` 已内部使用 bitmask + bit_iterator，确保 ABI 稳定：

```cpp
// forge_into 原型（修改 target，返回 cost）
int32_t forge_into(Item &target, const Item &sacrifice, const EnchReg &reg) const;

// 注册访问
reg.is_applicable(id);              // id 是 EnchReg::id_type (= uint8_t)
reg.get_conflict_mask(id);           // 返回 mask_type 冲突掩码
reg[id];                            // 返回 EnchInfo&
```
