# BestEnchSeq-Core 项目设计

> 版本：1.3 · 最后更新：2026-07-25

---

## 核心设计理念

### 1. 双层类型系统

项目区分两类数据类型，各自承担不同职责：

| 层面 | 命名空间 | 用途 | 特点 |
|------|---------|------|------|
| **Domain 类型** | 全局（`::`） | 输入解析、输出格式化、边界 I/O | 字符串 ID（NSID）、完整元数据、可抛异常 |
| **Compact 类型** | `algorithm::` | 算法热路径、forge 引擎 | `int16_t` 稠密 ID、连续内存、零异常路径 |

Domain 类型（`Item`、`EnchSet`、`EnchInfo` 等）是"胖对象"，包含字符串字段、校验逻辑。它们适合在 CLI/JSON 边界处使用，但效率不足以支撑算法对数百万状态的搜索。

Compact 类型（`algorithm::Item`、`algorithm::EnchSet`、`algorithm::Ench`）是"瘦值"，每个字段经过位宽裁剪，容器使用 `vector<Ench>` 而非 `unordered_set`。它们是为 L1 缓存行优化的算法内部表示。

**转换边界**：两种模型在 `CompactAdapter` 中互转，`main.cpp` 的管线中转换各发生一次。

### 2. 数据所有权通过值传递

`AlgorithmInput` 是一个值类型，内部包含所有执行所需的数据：

```cpp
struct AlgorithmInput {
    ForgeConfig f_config;              // 锻造配置（平台、忽略惩罚/上限等）
    SearchConfig s_config;             // 搜索配置（最大解数、内存限制等）
    algorithm::ItemCollection items;   // 装备 + 书
    algorithm::EnchCollection target;  // 目标附魔
    algorithm::EnchReg ench_reg;       // 剪枝后的紧凑注册表
};
```

没有指针、没有引用、没有全局单例依赖。`EnchReg` 作为值嵌入——传入 `AlgorithmExecutor` 后所有权一次性移交到工作线程。调用方不再持有任何共享状态。

**推论**：`EnchReg` 不再是单例。`CompactAdapter::apply()` 每调用创建独立的 `EnchReg`，用 `EnchReg::init()` 初始化后直接放入 `AlgorithmInput`。

### 3. 计算逻辑下沉到算法层

Domain 类型逐渐剥离计算职责，成为纯数据容器：

- `EnchSet` — 删除 `combine()`、`combine_s()`、`is_incompatible()`、`update_cache()`
- `Ench` — 删除 `operator+`
- `Item` — 删除 `update_cache()`、`get_penalty_cost()`

所有 forge 逻辑集中到 `IForgeEngine` 虚接口中，由 `ForgeEngine`（原版）或其子类（mod）实现。

**优势**：forge 规则变化只需修改 ForgeEngine，不用动 domain 类型。算法层通过 `reg[id].mul` / `reg[id].mul_b` / `reg.is_conflict()` 访问 compact 注册表，不接触 domain 注册表。

### 4. 虚接口可扩展性

`IForgeEngine` 定义完整的 forge 操作集合：

```
Core:      forge_into() / forge() / is_forgeable()
Sub-ops:   penalty_cost() / apply_cap() / estimate_forge_cost()
           // 书本乘数 mul_b 数据加载时预计算，不再需要 book_multiplier()
```

所有 sub-op 提供默认原版实现。Mod 只要继承 `IForgeEngine`，覆盖需要的部分：

```cpp
class ModForgeEngine : public IForgeEngine {
    int32_t penalty_cost(int8_t ppn) const noexcept override {
        // 自定义惩罚公式
    }
    // 其他方法使用默认实现
};
```

`estimate_forge_cost()` 默认实现调用了 `penalty_cost()` 并直接读取 `reg[id].mul`/`reg[id].mul_b`，所以覆盖这些 sub-op 会自动影响算法排序和启发式——无需改动算法代码。

### 5. 配置驱动行为

`ForgeConfig` 承载所有可调参数：

```cpp
struct ForgeConfig {
    bool ignore_penalty_cost = false;  // 禁用惩罚成本
    bool ignore_repair_cost  = false;  // 禁用修复成本
    bool ignore_cost_cap     = false;  // 禁用 39 级上限
    MCE  platform            = MCE::Java;  // 目标平台
};
```

平台不再通过全局单例设置。每种配置可以独立构造 `ForgeEngine` 实例，支持在同一个进程中评测不同平台/规则下的锻造序列。

---

## 系统架构

### 管线流程图

```
┌──────────────┐    ┌──────────────────────┐    ┌───────────────┐    ┌────────────┐
│  CLI / JSON  │───→│  CLIParser +         │───→│ CompactAdapter│───→│ Algorithm  │
│   输入解析    │    │  ItemResolver /      │    │ ::apply()     │    │  Executor  │
│              │    │  InventoryResolver   │    │               │    │            │
└──────────────┘    └──────────────────────┘    └───────┬───────┘    └─────┬──────┘
                                            │                  │
                                     ┌──────▼───────┐    ┌─────▼──────┐
                                     │ AlgorithmInput│    │ IAlgorithm │
                                     │ (compact)     │───→│ ::execute()│
                                     │ ench_reg      │    │ (compact)  │
                                     │ items, target │    └─────┬──────┘
                                     └──────────────┘          │
                                                          ┌─────▼──────┐
                                                          │ Algorithm  │
                                                          │ Output     │
                                                          │ (compact)  │
                                                          └─────┬──────┘
                                                                │
                                                ┌───────────────▼────┐
                                                │ CompactAdapter    │
                                                │ ::recall()        │
                                                │ (restore IDs)     │
                                                └───────┬───────────┘
                                                        │
                                            ┌───────────▼───────────┐
                                            │  OutputFormatter     │
                                            │  (domain Solution)   │
                                            └───────────────────────┘
```

### 数据加载管线

数据加载以 **Profile** 为单位，由 `ProfileLoader` + `RegistryLoader` 统一管理：

```
外部文件/目录
     │
     ├── FormatDetector::parse() 自动检测格式
     │     ├── NativeJson  (vanilla.json)
     │     ├── NativeCsv   (CSV 格式)
     │     └── McOfficial  (MC data-driven 格式)
     │
     ▼
EnchantmentData[] + EquipmentData[]  DTO 流
     │
     ▼
RegistryLoader::from_dto()
     │
     ├── EquipmentTagRegistry   (category string → NSID)
     ├── EquipmentRegistry      (category NSID resolved)
     └── EnchantmentRegistry    (exclusive/applicable resolved)
     │
     ▼
Profile (ench() + eq() + tags() 三元组)
```

内置数据通过 `ProfileLoader::load_builtin()` 加载，委托 `builtin/` 层读取嵌入的 vanilla.json。
`--registry-dir` / `--registries` CLI 选项调用 `FormatDetector` 自动识别格式（JSON / CSV / MC Official）。
数据源筛选（`--registries`）支持名称匹配和文件/目录路径两种指定方式。

### 注册表体系

算法域和业务域各自维护自己的注册表，中间通过 `CompactAdapter` 桥接：

```
Business domain (src/domain/business/registries/):
  EnchantmentRegistry (global, NSID keyed, full metadata)
  EquipmentRegistry (NSID keyed)
  EquipmentTagRegistry (tag-based equipment classification)
       │
       │ CompactAdapter::apply()
       ▼
Algorithm domain (src/domain/algorithm/registries/):
  EnchReg (flat conflict matrix, int16_t dense IDs)
       │
       │ owned by AlgorithmInput → executor → worker thread
       ▼
算法搜索: is_conflict(), reg[id].mul, reg[id].mul_b, reg[id].max_lvl
```

`CompactAdapter::apply()` 提取目标装备适用的附魔，构造 `CompactEnchInfo[]` 向量，用 `EnchReg::init()` 初始化为紧凑注册表。`EnchReg` 维护 `to_global_id()` / `to_local_id()` 双向映射。

### 算法策略体系

| 策略 | 类型 | 最优性 | 适用规模 | 核心机制 |
|------|------|--------|---------|---------|
| Greedy | 近似 | 否 | 任意 | 成本排序贪心 |
| Penalty Balance | 近似 | 否 | 任意 | 惩罚值最接近对合并 |
| Hierarchical | 近似 | 否 | 大量 | 分层分组 → 组内合并 |
| DiffFirst (difficulty_first) | 近似 | 否 | 任意 | PPN 分层，每层选最便宜对 |
| Hamming | 近似 | 否 | 大量 | Popcount 平衡二叉合并树 |
| DFS | 精确 | 是 | ≤ 8 | 迭代 B&B + 哈希记忆化 |
| A* | 精确 | 是 | ≤ 9 | 可采启发 + 优先队列 |
| IDA* | 精确 | 是 | ≤ 10 | 迭代加深 + TT 剪枝 |

所有算法共用 `IForgeEngine` 接口和 compact 类型系统。新算法只需实现 `IAlgorithm::execute()`，自动获得线程管理、暂停/取消、进度报告能力。

### 并发模型

`AlgorithmExecutor` 管理工作线程生命周期（`Idle → Running → Paused → Completed | Failed | Cancelled`）。工作线程持有 `ExecutionContext`，算法通过它报告进度、投递解方案、响应暂停/取消。

诊断事件通过 **`DiagnosticsService` 全局单例** 的异步管道传递：

```
算法线程 → ExecutionContext::report_progress() / report_solution()
        → DiagnosticsService::push()
        → try_post_emplace (placement new 到 BoundedMPMCQueue<DiagnosticsEvent, 64>)
        → EventLoop 线程 (atomic::wait 零 CPU 空闲)
        → DiagnosticsHandler
        → DiagnosticsWriter::write() (文件持久化，to_string 在此发生)
        → AlgorithmObserver::on_*() (异步回调)
```

`DiagnosticsEvent` 是 4-kind tagged variant（Exit / Progress / Solution / StateChange），每个事件携带 `task_id`，Observer 可通过 `accept_task_id()` 按任务过滤。所有字符串格式化（`to_string`）只在 EventLoop 线程的文件写入路径中发生，算法线程零开销。

---

## 模块职责（四域架构）

### `src/domain/orchestration/components/`（编排域）
**CompactAdapter** 是 domain ↔ compact 双向转换的唯一边界：
- `apply()`：验证输入 → 构造 CompactEnchInfo → init EnchReg → 转换物品 → 返回 `AlgorithmInput`
- `recall()`：遍历 compact steps → `to_global_id()` 恢复附魔 ID → 构建 `Solution`
- `from_domain()` / `to_domain()`：单物品转换

### `src/domain/algorithm/forge_engine/`（算法域）
**IForgeEngine**（虚接口）+ **ForgeEngine**（原版实现）：
- `forge_into()`：原地锻造（修改 target），返回成本
- `forge()`：非修改锻造，返回 `{result, cost}`
- `is_forgeable()`：物品可锻造性
- Sub-ops：`penalty_cost`、`apply_cap`、`estimate_forge_cost`（书本乘数由 `algorithm::EnchInfo::mul_b` 预计算）

### `src/domain/algorithm/types/`（算法类型）
算法层使用的紧凑数据类型：
- `algorithm::Ench`（int16_t id + level）
- `algorithm::EnchSet`（vector<Ench> 有序存储，lower_bound 插入）
- `algorithm::Item`（type + dur + ppn + enchs）
- `algorithm::EnchStep`（base + sacrifice + cost）
- `algorithm::CompactEnchInfo`（mul + max_lvl + exc_mask + applicable）

### `src/domain/algorithm/registries/`（算法注册表）
**algorithm::EnchReg**：预计算针对特定装备的紧凑注册表：
- N×N 扁平冲突矩阵（`vector<char>`）
- 预计算 `CompactEnchInfo[]`（multiplier、max_level、applicable、exc_mask）

### `src/domain/business/`（业务域）

业务域是自包含的核心域，以 **Profile** 为操作的一等公民：

```
business/
├── types/             值类型 + Profile + DTO
├── registries/        纯数据容器（EnchantmentRegistry / EquipmentRegistry / EquipmentTagRegistry）
├── parsers/           格式解析器（NativeJson / NativeCsv / McOfficial）
├── loaders/           RegistryLoader（DTO↔Registry）+ ProfileLoader（Profile I/O）
├── managers/          RegistryManager（筛选/集合运算）+ ProfileManager（生命周期/快照/分支）
└── components/        Serializer (thin ADL layer) + FormatDetector + TagResolver
```

业务域类型继承 `IJsonSerializable`（定义在 `common/serialization/`），各自实现 `to_json()` / `from_json()`。`Serializer` 的 operator 作为薄委托层保留以兼容 ADL。

**Profile** 是核心业务单元：
- 所有正常业务操作以 Profile 为输入输出
- 跨注册表操作通过 Profile 代理方法完成
- 支持快照、分支、合并、集合运算（`| & + -` 运算符）

**RegistryManager** 提供注册表级别操作：筛选、集合运算、diff、验证。
**ProfileManager** 提供 Profile 生命周期管理：CRUD、激活、快照、分支、合并。

---

## 错误处理策略

- **输入验证**：`CompactAdapter::apply()` 内部检查所有输入数据的语义正确性，聚合所有错误后抛出 `std::invalid_argument`
- **算法执行**：异常被 `AlgorithmExecutor` 的 worker 线程捕获，状态置为 `Failed`
- **forge 操作**：不抛异常——所有检查在 apply() 中完成，算法假设输入已通过校验

---

## 参考

- `src/domain/orchestration/components/CompactAdapter.h/.cpp` — 边界转换
- `src/domain/algorithm/forge_engine/IForgeEngine.h` — forge 接口
- `src/domain/algorithm/forge_engine/ForgeEngine.h/.cpp` — 原版实现
- `src/domain/algorithm/types/CompactedTypes.h/.cpp` — 紧凑类型
- `src/domain/algorithm/registries/EnchReg.h/.cpp` — 紧凑注册表
- `src/domain/business/registries/EnchantmentRegistry.h/.cpp` — 业务注册表
- `src/domain/business/types/Profile.h/.cpp` — Profile 一等公民
- `src/domain/business/loaders/ProfileLoader.h/.cpp` — Profile 加载/导出
- `src/domain/business/managers/ProfileManager.h/.cpp` — Profile 生命周期管理
- `src/domain/business/managers/RegistryManager.h/.cpp` — 注册表集合运算
- `src/common/serialization/ISerializable.h` — 序列化通用根接口
- `src/common/serialization/IJsonSerializable.h` — JSON 序列化接口（to_json/from_json）
- `src/common/serialization/IBinarySerializable.h` — 二进制序列化接口（ByteStream）
- `src/domain/business/components/Serializer.h/.cpp` — ADL 兼容的序列化 delegate
- `docs/domain_designs/business-domain-design.md` — 业务域详细设计
- `src/domain/algorithm/AlgorithmExecutor.h/.cpp` — 执行引擎
- `src/domain/algorithm/_strategies/` — 8 种算法策略
- `docs/algorithm-design-discussion.md` — 算法设计详细探讨
- `docs/anvil-mechanics-reference.md` — 铁砧机制参考
- `docs/MPMCQueue.md` — MPMC/SPSC 无锁队列设计、正确性证明与性能模型
