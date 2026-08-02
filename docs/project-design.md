# BestEnchSeq-Core 项目设计

> 版本：2.2 · 最后更新：2026-08-02

---

## 核心设计理念

### 1. 四域架构

项目采用**四域架构**，由底层到顶层依次为：

| 域 | 命名空间 | 职责 | 依赖 |
|------|---------|------|------|
| `common/*` | — | 5 个独立子库（core/io/log/i18n/cli） | 无（各自独立） |
| `domain/algorithm/` | `algorithm::` | 紧凑类型、锻造引擎、搜索策略、诊断 | `common-core` + `log` |
| `domain/business/` | `::` | 业务类型、注册表、Profile、解析器、加载器 | `common-core` + `io` + `log` |
| `domain/orchestration/` | `orchestration::` | Pipeline、CompactAdapter、格式化器 | `algorithm` + `business` |
| `domain/interface/` | `::` | BesqContext、CLIApp、C ABI | `orchestration` + `common` 子库 |

### 2. 双层类型系统

项目区分两类数据类型，各自承担不同职责：

| 层面 | 命名空间 | 用途 | 特点 |
|------|---------|------|------|
| **Domain 类型** | 全局（`::`） | 输入解析、输出格式化、边界 I/O | 字符串 ID（NSID）、完整元数据、可抛异常 |
| **Compact 类型** | `algorithm::` | 算法热路径、forge 引擎 | `uint8_t` 稠密 ID、固定 size 数组（64×64 冲突矩阵）、零异常路径 |

Domain 类型（`Item`、`EnchSet`、`EnchInfo` 等）是"胖对象"，包含字符串字段、校验逻辑。它们适合在 CLI/JSON 边界处使用，但效率不足以支撑算法对数百万状态的搜索。

Compact 类型（`algorithm::Item`、`algorithm::EnchSet`、`algorithm::Enchantment`）是"瘦值"，每个字段经过位宽裁剪：附魔 ID 和等级均为 `uint8_t`，`EnchSet` 使用 `uint64_t` 位掩码 + `uint8_t[64]` 等级数组（`O(1)` 查找），`EnchReg` 冲突矩阵为固定 `std::array<char, 64×64>`。它们是为 L1 缓存行优化的算法内部表示。

**转换边界**：两种模型在 `CompactAdapter` 中互转，`main.cpp` 的管线中转换各发生一次。

### 3. 数据所有权通过值传递

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

### 4. 计算逻辑下沉到算法层

Domain 类型是纯数据容器，不包含 forge 计算逻辑：

- `EnchSet` 不包含 `combine()`、`combine_s()`、`is_incompatible()` 等方法
- `Ench` 不包含 `operator+`
- `Item` 不包含 `update_cache()`、`get_penalty_cost()`

所有 forge 逻辑集中在 `IForgeEngine` 虚接口中，由 `ForgeEngine`（原版）或其子类（mod）实现。

**优势**：forge 规则变化只需修改 ForgeEngine，不用动 domain 类型。算法层通过 `reg[id].mul` / `reg[id].mul_b` / `reg.is_conflict()` 访问 compact 注册表，不接触 domain 注册表。

### 5. 虚接口可扩展性

`IForgeEngine` 定义完整的 forge 操作集合：

```
Core:      forge_into() / forge() / is_forgeable() / pure_forge_into()
Sub-ops:   penalty_cost() / estimate_forge_cost()
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

### 6. Profile 一等公民

`Profile` 是**所有正常业务操作的基本单位**：

- Profile 同时持有 `EnchantmentRegistry`、`EquipmentRegistry`、`TagRegistry`
- 所有 Pipeline 接收 Profile 或 ProfileManager，不接收裸注册表
- 支持快照、分支、合并、集合运算（`| & + -`）
- JSON 序列化格式与 vanilla.json 兼容

### 7. Pipeline 模式

每个 Pipeline 是**带单个 `run()` 方法的纯结构体**。不自注册、不虚分派——`BesqContext` 或 `main.cpp` 通过 switch 或直接调用分派。

```cpp
struct XxxPipeline {
    static XxxResult run(/* domain dependencies */, const XxxRequest& request);
};
```

### 8. 配置驱动行为

`ForgeConfig` 承载所有可调参数：

```cpp
struct ForgeConfig {
    bool ignore_penalty_cost = false;  // 禁用惩罚成本
    bool ignore_repair_cost  = false;  // 禁用修复成本
    MCE  platform            = MCE::Java;  // 目标平台
};
```

平台不再通过全局单例设置。每种配置可以独立构造 `ForgeEngine` 实例，支持在同一个进程中评测不同平台/规则下的锻造序列。

---

## 系统架构

### 主数据流

```
外部输入（CLI 参数 / C ABI 调用）
  → interface/cli/parse_cli()          CLIConfig
  → interface/cli/EnchParser           字符串 → EnchSet
  → interface/cli/ItemParser           字符串 → Item
  → 组装 SolveRequest                  (orchestration/types/SolveRequest.h)
  → orchestration/SolvePipeline::run()
       │
       ├─ stage_apply()
       │   CompactAdapter::apply(profile, request)
       │     → AlgorithmInput { ench_reg, target, items, config }
       │
       ├─ stage_execute()
       │   → loader.create(algorithm)
       │   → resolver.resolve(input)
       │   → executor.start(input)     (异步)
       │   → AlgorithmOutput
       │
       └─ stage_recall()
           → CompactAdapter::recall(output, input)
           → SolveResult { solutions, algorithm_used, timing }
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
EnchantmentData[] + EquipmentData[] DTO 流
     │
     ▼
RegistryLoader::from_dto()
     │
     ├── TagRegistry            (真实 MC tag 定义：物品/enchantable/* / 附魔 tag)
     ├── EquipmentRegistry      (category 显示短名)
     └── EnchantmentRegistry    (exclusive / supported_items 交叉验证)
     │
     ▼
Profile (ench() + eq() + tags() 三元组)
```

内置数据通过 `ProfileLoader::load_builtin()` 加载，委托 `builtin/DataLoader` 层读取嵌入的 vanilla.json。
`--registry-dir` / `--registries` CLI 选项调用 `FormatDetector` 自动识别格式（JSON / CSV / MC Official）。

### 注册表体系

算法域和业务域各自维护自己的注册表，中间通过 `CompactAdapter` 桥接：

```
Business domain (src/domain/business/registries/):
  EnchantmentRegistry (NSID keyed, full metadata, mutable)
  EquipmentRegistry (NSID keyed)
  TagRegistry (real MC item/enchantment tag definitions)
       │
       │ CompactAdapter::apply()
       ▼
Algorithm domain (src/domain/algorithm/registries/):
  EnchReg (fixed 64×64 conflict matrix, uint8_t dense IDs, pruned per target)
       │
       │ owned by AlgorithmInput → executor → worker thread
       ▼
算法搜索: is_conflict(), reg[id].mul, reg[id].mul_b, reg[id].max_lvl
```

`CompactAdapter::apply()` 提取目标装备适用的附魔，构造 `CompactEnchInfo[]` 向量，用 `EnchReg::init()` 初始化为紧凑注册表。`EnchReg` 维护 `to_global_id()` / `to_local_id()` 双向映射。

### 国际化 (i18n)

采用自定义轻量字符串表方案（`common/i18n/Language.h/.cpp`）。翻译数据以 JSON 格式存储在 `data/i18n/`，通过 `EmbedResource.cmake` 编译时嵌入二进制。所有用户可见输出（CLI 帮助文本、错误消息、锻造方案输出）使用 `tr("key")` / `tr_fmt("key", ...)`。

语言选择三级降级：`--lang` CLI 标志 > `BESQ_LANG` 环境变量 > 系统 locale 自动检测。先支持中文 (`zh_CN`) 和英文 (`en_US`)，架构可扩展。机器可读格式（compact、JSON）和日志不翻译。

### 算法策略体系

| 策略 | 类型 | 最优性 | 适用规模 | 来源 | 核心机制 |
|------|------|--------|---------|------|---------|
| Greedy | 近似 | 否 | 任意 | 插件 | 成本排序贪心 |
| Penalty Balance | 近似 | 否 | 任意 | 插件 | 惩罚值最接近对合并 |
| Hierarchical | 近似 | 否 | 大量 | 插件 | 分层分组 → 组内合并 |
| DiffFirst | 近似 | 否 | 任意 | 插件 | PPN 分层，每层选最便宜对 |
| Hamming | 近似 | 否 | 大量 | 内置 | Popcount 平衡二叉合并树 |
| dp_merge | 精确 | 是 | ≤ 16 | 内置 | 分治 DP + (EnchSet, PPN) Pareto 分桶 |
| bb_dp | 精确 | 是 | ≤ 24 | 内置 | B&B 分治 DP + Pareto + 可选 39 级上限 |
| DFS | 精确 | 是 | ≤ 8 | 插件 | 迭代 B&B + 哈希记忆化 |
| A* | 精确 | 是 | ≤ 9 | 插件 | 可采启发 + 优先队列 |
| IDA* | 精确 | 是 | ≤ 10 | 插件 | 迭代加深 + TT 剪枝 |

所有算法共用 `IForgeEngine` 接口和 compact 类型系统。新算法只需实现 `IAlgorithm::execute()`，自动获得线程管理、暂停/取消、进度报告能力。

### Pipeline 架构

| Pipeline | 职责 | 关键依赖 |
|----------|------|---------|
| `SolvePipeline` | 锻造求解（apply → execute → recall） | CompactAdapter, AlgorithmLoader, AlgorithmExecutor |
| `ManagePipeline` | Profile/注册表管理 | ProfileManager, ProfileLoader, RegistryHelper |
| `ExportPipeline` | 数据导出 | EnchSerializer, OutputFormatter |

### 并发模型

`AlgorithmExecutor` 管理工作线程生命周期（`Idle → Running → Paused → Completed | Failed | Cancelled`）。工作线程持有 `ExecutionContext`，算法通过它报告进度、投递解方案、响应暂停/取消。

诊断事件通过 **`DiagnosticsService` 全局单例**的异步管道传递：

```
算法线程 → ExecutionContext::report_progress() / report_solution()
        → DiagnosticsService::push()
        → try_post_emplace (placement new 到 BoundedMPMCQueue<DiagnosticsEvent, 64>)
        → EventLoop 线程 (atomic::wait 零 CPU 空闲)
        → DiagnosticsHandler
        → DiagnosticsWriter::write() (文件持久化)
        → AlgorithmObserver::on_*() (异步回调)
```

`DiagnosticsEvent` 是 4-kind tagged variant（Exit / Progress / Solution / StateChange），每个事件携带 `task_id`，Observer 可通过 `accept_task_id()` 按任务过滤。所有字符串格式化只在 EventLoop 线程的文件写入路径中发生，算法线程零开销。

---

## 模块职责（四域架构）

### `src/builtin/`（内置数据层）

项目级内置数据工具，由 `ProfileLoader::load_builtin()` 调用。

- `DataLoader`：读取编译时嵌入的 `vanilla.json` → 输出 DTO 流
- `EmbeddedData`：声明由 CMake `EmbedResource.cmake` 嵌入的二进制资源
- `ItemProperties`：原版物品属性定义（耐久度、最大合并等级等）

### `src/common/`（共享工具层）

零业务知识的通用工具集，所有域都可以使用。

**io/**：JSON 手写递归下降解析器（零外部依赖）、CSV 读写、ByteStream 二进制流
**log/**：Logger 全局异步日志（SegmentedMPSCQueue + EventLoop + atomic::wait 零 CPU 空闲）
**serialization/**：`ISerializable` → `IJsonSerializable` / `IBinarySerializable` 序列化接口层级
**utils/**：EventLoop、MemoryPool、ObjectPool、FlatHashMap、HashUtils、StringUtils、EnvUtil、ExpCalculator、无锁队列家族（BoundedMPMC/MPSC、SegmentedMPMC/MPSC、SPSC、SPMC）

### `src/domain/algorithm/`（算法域）

零业务/接口依赖，仅使用 compact 类型。

**核心接口**：
- `IAlgorithm` — 算法策略虚接口
  - `name()` / `version()` — 标识
  - `evaluate(n)` — 预估 n 个附魔的计算时间（ms，`double`）
  - `execute(input, ctx)` — 执行搜索
  - `process(solution)` — 重放步骤计算最终物品
  - `resolve(input)` — 预解析输入生成候选物品
  - `simulate(input)` — 快速可行性检查
  - `get_forge_engine()` — 返回副本（`unique_ptr<IForgeEngine>`）
  - `get_serializer()` — 断点序列化（可选）
  - `supported_mode()` / `is_resumable()` — 能力声明
- `AlgorithmExecutor` — 异步执行引擎（线程管理 + 状态机）
- `ExecutionContext` — 一站式算法交互接口（控制 + 计数器 + 进度 + 方案累积）

**types/**：紧凑类型系统
- `algorithm::Enchantment` (uint8_t id + level, 2 bytes)
- `algorithm::EnchSet` (uint64_t bitmask + uint8_t[64] level array, O(1) lookup)
- `algorithm::Item` (type + dur + ppn + enchs)
- `algorithm::Equipment` (id + max_durability + applicable_tags)
- `AlgorithmInput` / `AlgorithmOutput` / 配置类型

**forge_engine/**：`IForgeEngine`（虚接口）+ `ForgeEngine`（原版实现）
**registries/**：`EnchReg`（固定 64×64 冲突矩阵 + 掩码缓存 + 紧凑注册表）、`AlgorithmRegistry`（工厂）
**_strategies/**：内置策略（Hamming、dp_merge、bb_dp），通过 `Registration.h` 自动注册
**components/**：Heuristic、ItemPool、SearchUtils、StepTree 等共享搜索基础设施
**diagnostics/**：事件驱动诊断管道（`DiagnosticsService` + `IAlgorithmObserver` + `DiagnosticsWriter`）
**serialization/**：二进制 checkpoint（`IAlgorithmSerializer` + `Checkpoint`）
**plugin/**：`AlgorithmLoader`（内置注册 + 插件热加载）
**resolvers/**：`ItemResolver`、`InventoryResolver`（算法级解析辅助）

### `src/domain/business/`（业务域）

自包含的核心域，以 **Profile** 为操作的一等公民。

**types/**：值类型（`Ench`、`EnchInfo`、`EnchSet`、`Item`、`Equipment`、`EquipmentTag`、`Solution`、`Profile`）+ DTO（`EnchantmentData`、`EquipmentData`）
**registries/**：纯数据容器（`EnchantmentRegistry`、`EquipmentRegistry`、`TagRegistry`，均继承自 `IRegistry`）
**parsers/**：格式解析器（`NativeJsonParser`、`NativeCsvParser`、`McOfficialParser`，共享 `ParserShared`）
**loaders/**：DTO ↔ 注册表/Profile 转换（`RegistryLoader`、`ProfileLoader`）
**ProfileManager**（业务域顶层）：Profile 生命周期与依赖图/有效视图（`| & + -` 运算符位于 `components/RegistryHelper`）
**components/**：`FormatDetector`（格式检测+自动分派）、`Serializer`（JSON ADL 委托）、`TagResolver`（标签解析）、`RegistryHelper`（集合运算，原 `RegistryManager`）

### `src/domain/interface/`（接口域）

纯翻译层，无业务逻辑。

- `BesqContext`：应用会话外观，持有 ProfileManager 和 AlgorithmLoader，委托所有操作到 orchestration pipeline
- **cli/**：`CLIApp`（CLI 入口）、`EnchParser`（`"sharpness=5"` → EnchSet）、`ItemParser`（`"diamond_sword[...]"` → Item）；`--registry-edit` 解析内联在 CLIApp 中
- **components/**：公共组件预留（暂无内容）
- **abi/**：`CAbiBindings`（C ABI 包装，JSON 交换）

### `src/domain/orchestration/`（编排域）

跨域胶水层，拥有管线编排。

**types/**：Pipeline 契约（`SolveRequest` / `SolveResult`、`ManageRequest` / `ManageResult`、`ExportRequest` / `ExportResult`）
**pipelines/**：任务协调器（`SolvePipeline` 3 阶段锻造求解、`ManagePipeline` 管理分派、`ExportPipeline` 导出分派）
**components/**：`CompactAdapter`（domain ↔ compact 双向转换）、`OutputFormatter`（Solution → text/compact/json）、`EnchSerializer`（注册表 JSON/CSV/MC Official 序列化）

---

## 错误处理策略

- **输入验证**：`CompactAdapter::apply()` 内部检查所有输入数据的语义正确性，聚合所有错误后抛出 `std::invalid_argument`
- **算法执行**：异常被 `AlgorithmExecutor` 的 worker 线程捕获，状态置为 `Failed`
- **forge 操作**：不抛异常——所有检查在 apply() 中完成，算法假设输入已通过校验

---

## 参考

项目关键文件与设计文档索引：

- `src/common/serialization/ISerializable.h` — 序列化通用根接口
- `src/common/serialization/IJsonSerializable.h` — JSON 序列化接口
- `src/common/serialization/IBinarySerializable.h` — 二进制序列化接口
- `src/domain/algorithm/IAlgorithm.h` — AlgorithmInput/Output + IAlgorithm 接口
- `src/domain/algorithm/AlgorithmExecutor.h/.cpp` — 异步执行引擎
- `src/domain/algorithm/ExecutionContext.h/.cpp` — 执行控制上下文
- `src/domain/algorithm/types/Enchantment.h/.cpp` — 紧凑类型
- `src/domain/algorithm/registries/EnchReg.h/.cpp` — 紧凑注册表
- `src/domain/algorithm/forge_engine/IForgeEngine.h` — forge 接口
- `src/domain/algorithm/forge_engine/ForgeEngine.h/.cpp` — 原版实现
- `src/domain/algorithm/_strategies/` — 内置算法策略
- `src/domain/business/types/Profile.h/.cpp` — Profile 一等公民
- `src/domain/business/loaders/ProfileLoader.h/.cpp` — Profile 加载/导出
- `src/domain/business/ProfileManager.h/.cpp` — Profile 生命周期管理
- `src/domain/business/components/RegistryHelper.h/.cpp` — 注册表集合运算
- `src/domain/business/components/Serializer.h/.cpp` — ADL 兼容的序列化 delegate
- `src/domain/orchestration/components/CompactAdapter.h/.cpp` — 边界转换
- `src/domain/orchestration/components/OutputFormatter.h/.cpp` — 输出格式化
- `src/domain/orchestration/components/EnchSerializer.h/.cpp` — 注册表序列化
- `src/domain/orchestration/pipelines/SolvePipeline.h/.cpp` — 锻造求解管线
- `src/domain/orchestration/pipelines/ManagePipeline.h/.cpp` — 管理管线
- `src/domain/orchestration/pipelines/ExportPipeline.h/.cpp` — 导出管线
- `docs/domain_designs/business-domain-design.md` — 业务域详细设计
- `docs/domain_designs/orchestration-domain-design.md` — 编排域详细设计
- `docs/domain_designs/interface-domain-design.md` — 接口域详细设计
- `docs/algotithm_designs/algorithm-design-discussion.md` — 算法设计详细探讨
- `docs/algotithm_designs/hamming-algorithm-design.md` — Hamming 算法设计
- `docs/component_designs/MPMCQueue.md` — 无锁队列设计
- `docs/mc/anvil-mechanics-reference.md` — 铁砧机制参考
- `docs/json-output-schema.md` — JSON 输出格式规范
- `docs/data-sources.md` — 数据来源文档
