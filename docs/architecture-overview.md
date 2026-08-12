# BestEnchSeq-Core 架构总览

> 面向新开发者的入口文档：约 30 分钟建立全局认知。
> **事实以代码为准**——本文最后核对于 2026-08-13，与 `CLAUDE.md`（开发操作指南）、`docs/project-design.md`（设计理念详述）、`docs/domain_designs/*`（各域详细设计）互补。

---

## 1. 这是什么

C++20 实现的 Minecraft 附魔锻造序列规划器：给定**目标**（`--target`，期望最终附魔）与**起点**（`--source` 源附魔或库存物品），搜索成本最优的附魔书锻造顺序，并输出逐步锻造方案。约束包括铁砧惩罚（prior work penalty）、魔咒冲突、装备适用性（tag）、平台差异（Java/Bedrock）、Too Expensive 上限（39 级）等。

三个构建产物共享同一核心：

| 产物 | 门控 | 用途 |
|---|---|---|
| `besq` | 默认 | 命令行工具（CLI + C ABI） |
| `besq-gui` | `BESQ_BUILD_GUI=ON` | 本地 Web GUI（REST API + SSE，浏览器访问） |
| `besq-worker` | `BESQ_BUILD_SANDBOX=ON` | 沙箱子进程，承载第三方插件算法的隔离执行 |

核心卖点：**数据驱动**（vanilla/mod 数据表、MC 官方 datapack、Profile 依赖体系）、**算法可插拔**（内建 + 插件热加载 + 审计/沙箱）、**零第三方依赖**（纯标准库 + 自研 HTTP/JSON/i18n/并发组件）。

### 30 秒上手

```bash
# 构建（Clang + Ninja；Windows 亦可用 MSVC）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 运行
./build/bin/besq --target "diamond_sword[sharpness=5,knockback=2]" --source "sharpness=2"

# 测试 / 基准
ctest --test-dir build --output-on-failure
cmake --build build --target forge_benchmark
./build/bin/forge_benchmark --group sword --algo dp_merge,bb_dp
```

---

## 2. 总体架构：四域 + 共享层

```
┌─────────────────────────────────────────────────────────────────────┐
│ 宿主（hosts）: besq (CLI) · besq-gui (Web GUI) · besq-worker (沙箱)  │
└───────────────┬─────────────────────────────────────────────────────┘
                ▼
  ┌───────────────────────────────────────────────┐
  │ interface/   I/O 边界：BesqContext、CLI、C ABI │   ──► 依赖 orchestration + common
  └───────────────┬───────────────────────────────┘
                  ▼
  ┌───────────────────────────────────────────────┐
  │ orchestration/ 跨域胶水：3 条管线 + CompactAdapter │   ──► 依赖 algorithm + business
  └───────────────┬───────────────────────────────┘
                  ▼
  ┌──────────────────────┬──────────────────────┐
  │ algorithm/ 算法内核    │ business/ 业务域      │
  │ 紧凑类型·锻造引擎·策略   │ 注册表·Profile·解析器  │
  └───────────┬──────────┴───────────┬──────────┘
              ▼                       ▼
  ┌───────────────────────────────────────────────┐
  │ common/  共享工具（零业务知识）                   │
  │ core · io · log · i18n · cli · thread          │
  │ ds/（header-only schema 引擎）                  │
  └───────────────────────────────────────────────┘
  builtin/（编译期嵌入 vanilla.json / item_properties / i18n）
```

### 域职责与依赖约束（红线）

| 域 | 命名空间 | 职责 | 允许依赖 | **禁止** |
|---|---|---|---|---|
| `common/` | `::`/`besq::` | NSID/MCE、JSON DOM、日志、i18n、队列、CLIParser、schema 引擎 | — | 任何域 |
| `domain/algorithm/` | `algorithm::` | 紧凑类型、EnchReg、ForgeEngine、搜索策略、诊断、checkpoint、插件加载、沙箱 | common | business、orchestration、interface |
| `domain/business/` | `::` | EnchInfo/Item/Solution/Profile、三个注册表、解析器、ProfileManager | common、builtin | algorithm、orchestration、interface |
| `domain/orchestration/` | `orchestration::` | Solve/Manage/Export 管线、CompactAdapter、OutputFormatter | algorithm、business、common | interface |
| `domain/interface/` | `::` | BesqContext、CLIApp、C ABI、HTTP 框架、Web 模块 | orchestration、business、algorithm（诊断）、common | — |

> `builtin/` 是唯一例外：它既是数据层（供 business 消费）又引用 business 类型（`DataLoader` 输出注册表），与 business 构成真实静态循环——CMake 用 `-Wl,--start-group` 包住整条链解决。

### 构建产物与"共享内核"原则

- **`besq-algo-core` 是 SHARED 库**，且是全项目唯一进程内副本：CLI、`besq-worker`、插件三方链接同一份 `.dll/.so`，保证 `IAlgorithm` vtable、`DiagnosticsService` 单例、堆分配器唯一（历史上"静态内核 + -rdynamic"方案在 dlopen 时 SEGV，已弃用）。
- **`besq-common-log` 也是 SHARED**：Logger 单例每进程必须一份。
- `besq-core` 是 STATIC 聚合（algorithm + business + orchestration + builtin + 内嵌数据），接口域由宿主显式补充。
- 插件构建：宿主 configure 时渲染 `build/besq-coreConfig.cmake`，导出唯一 IMPORTED 目标 `besq-algo-core::besq-algo-core` 与诊断宏定义（保证与宿主 ODR 一致）。插件强制与宿主**同编译器、同构建类型**。

---

## 3. 目录导览

```
src/
├── main.cpp                  # CLI 入口（apply_lang → CLIApp::run）
├── AppConfig.h               # 运行时配置（env 读取：BESQ_LANG/SANDBOX/GUI_*…）
├── BuildConfig.h.in          # 生成头（BESQ_VERSION 等）
├── builtin/                  # DataLoader / ItemProperties / I18nLoader + 嵌入资源访问器
├── common/                   # 共享工具（6 个独立库）
│   ├── ds/  i18n/  io/  log/  serialization/  utils/（含 cli/CLIParser v2、queue/、thread/）
├── domain/
│   ├── algorithm/            # 算法域（besq-algo-core）
│   │   ├── types/            #   紧凑类型（Ench/EnchSet/Item/AlgorithmInput…）
│   │   ├── registries/       #   EnchReg（64×64 位冲突矩阵）+ AlgorithmRegistry
│   │   ├── forge_engine/     #   IForgeEngine + ForgeEngine（Java/Bedrock 成本模型）
│   │   ├── _strategies/      #   内建算法：dp_merge / bb_dp / hamming（CMake 自动注册）
│   │   ├── components/       #   Heuristic/ItemPool/SearchUtils/StepTree
│   │   ├── diagnostics/      #   事件驱动诊断（observer/Writer/Service）
│   │   ├── serialization/    #   Checkpoint + IAlgorithmSerializer
│   │   ├── plugin/           #   AlgorithmLoader + PluginAudit（ELF/PE 静态审计）
│   │   ├── sandbox/          #   SandboxedExecutor + IpcProtocol（分块帧协议）
│   │   └── resolvers/        #   IResolver + DefaultResolver
│   ├── business/             # 业务域
│   │   ├── types/ dto/       #   EnchInfo/Item/Solution/Profile/DTO
│   │   ├── registries/       #   IRegistry<T> + Enchantment/Equipment/Tag 注册表
│   │   ├── parsers/          #   NativeJson / NativeCsv / McOfficial（datapack）
│   │   ├── loaders/          #   RegistryLoader / ProfileLoader
│   │   ├── schemas/          #   ds:: 声明式 schema（序列化单一事实源）
│   │   ├── components/       #   Serializer / FormatDetector / TagResolver / LimitedLevelCalculator / RegistryHelper
│   │   └── ProfileManager.h  #   生命周期/依赖图/有效视图/undo/publish
│   ├── orchestration/        # 编排域
│   │   ├── pipelines/        #   SolvePipeline（apply→execute→recall）/ Manage / Export
│   │   ├── components/       #   CompactAdapter（域桥）/ OutputFormatter / EnchSerializer
│   │   └── types/            #   Solve/Manage/Export Request+Result
│   └── interface/            # 接口域
│       ├── BesqContext.h     #   会话门面（pImpl，持三个管理器 + 活动 executor 句柄）
│       ├── cli/              #   CLIApp / EnchParser / ItemParser / InventoryParser
│       ├── abi/              #   CAbiBindings（besq_* 导出）
│       ├── components/http/  #   可复用 HTTP 框架（besq-http）：Server/Reactor/Connection/Router/SSE
│       └── web/              #   WebModule（8 控制器）+ WebSolveService + SseHub
├── worker/                   # besq-worker 沙箱子进程（dlopen → seccomp → serve IPC）
└── gui/                      # besq-gui 主程序（HttpServer + WebModule 装配）

include/besq/besq.h           # 公开 C ABI 头
gui/frontend/                 # SPA（vanilla JS + mdui，hash 路由，编译期嵌入）
plugins/                      # 外部策略（astar/dfs/idastar/diff_first/penalty_balance/malicious）
data/                         # builtin/vanilla.json、i18n/、tests/（profiles、testcases、datapack）
tests/                        # 按域分目录，~90 个测试二进制（共享 test_framework.h）
benchmarks/                   # forge_benchmark + harness（bench_framework.h）
docs/                         # 本文档、project-design、domain_designs/、algotithm_designs/ 等
```

---

## 4. 核心数据模型

### 4.1 基础类型（common/CommonTypes.h）

- **NSID**：`namespace:id` 字符串；`#` 前缀 = tag；空 namespace 默认 `minecraft`；校验遵循 MC Java 标识符规则
- **MCE**：`{None, Java, Bedrock, All}`（平台）
- **AlgorithmMode**：`{direct, inventory}`（bitmask）

### 4.2 双层类型系统（最重要的设计决策之一）

| | 业务域（Domain） | 算法域（Compact） |
|---|---|---|
| 键 | NSID 字符串 | `uint8_t` 稠密局部 id（≤64） |
| 形态 | 胖对象：完整元数据、可抛异常 | 瘦值：trivially-copyable + `static_assert` 编译期契约 |
| 用途 | 输入解析、输出格式化、边界 I/O | 算法热路径（百万级状态搜索） |
| 示例 | `EnchInfo`（multiplier/exclusive_set/supported_items…） | `Ench`（2B）、`EnchSet`（88B）、`Item` |

**紧凑类型要点**：
- `EnchSet`：`uint8_t[64]` 等级数组 + size + 位掩码 + **惰性哈希缓存**（88B；线上序列化恰 72B）；迭代器按位掩码跳空槽（`std::countr_zero`）
- `EnchReg`：`std::array<mask_type, 64>` **行掩码缓存**——每行一个 uint64，即 64×64 位冲突矩阵（512B），`get_conflict_mask(id)` O(1)；由单向 `exc_mask` 对称并集重建
- `AlgorithmInput` 拥有全部数据（注册表/物品/目标/配置按值），无外部生命周期

**桥接**：`orchestration/components/CompactAdapter` 是唯一转换点——`apply()`（业务→紧凑：NSID 排序 → 平台/tag 过滤 → 裁剪 ≤64 个）与 `recall()`（紧凑→业务：还原 NSID、重建 `Solution`、折算经验成本）。

### 4.3 Profile 一等公民（业务域核心）

```
ProfileMetadata(name 任意字符串 key, display_name, dependencies[], …)
Profile = ProfileMetadata + EnchantmentRegistry + EquipmentRegistry + TagRegistry + TagResolver(运行时)
```

- 管线只收 `Profile`/`ProfileManager`，绝不传裸注册表
- **依赖图**：`dependencies` DFS 求传递闭包（拓扑序、环→空/拒绝）
- **有效视图** `resolve_effective()`：隐式 `builtin:vanilla` 根（最低优先级）→ 依赖链（拓扑序）→ 自身（最高），`RegistryHelper::merge` 逐层覆盖 + 合并 tag 宇宙的 `TagResolver`
- **事务式变更** `_mutate`：validate-before → Json 快照入 undo 栈 → 应用 → validate-after（失败回滚）
- **加载链**：文件 → `FormatDetector` → 解析器 → DTO → `RegistryLoader::resolve_own_content()`（两阶段：seed vanilla 宇宙 → 交叉验证 → 过滤回自身）→ `LimitedLevelCalculator` 回填 → Profile

---

## 5. 接口面

### 5.1 域内核心接口（算法域）

```cpp
// 算法域唯一对外入口 —— 沙箱缝在这里
class IExecutor {
    virtual void start(AlgorithmInput) / start(checkpoint);   // 全新 / 恢复
    virtual void pause() / resume() / cancel();               // pause 同步等算法 ack
    virtual AlgorithmState wait(); state(); progress();
    virtual AlgorithmOutput output();                         // 仅 Completed 有效
    virtual std::vector<uint8_t> serialize_state();           // 仅 Paused
};

class IAlgorithm {
    virtual name / version / supported_mode / is_resumable;
    virtual double evaluate(int16_t ench_count);              // 预测秒数，0=确定性
    virtual void init(input, ctx);                            // 预热（默认空）
    virtual void execute(input, ctx) = 0;                     // 策略主体
    virtual simulate(input); get_forge_engine(); get_resolver(); get_serializer();
};

class IForgeEngine {
    forge_into / forge / is_forgeable / pure_forge_into;      // 核心锻造操作
    penalty_cost / estimate_forge_cost;                       // 子操作（有 vanilla 默认）
};
```

- `AlgorithmExecutor`：异步状态机 `Idle → Running → Pausing → Paused → Completed | Failed | Cancelled` + 超时 watcher + warmup；暂停确认握手（flag-then-notify-under-lock）保证 checkpoint 静止点
- `ExecutionContext`：算法与执行器的交互面——`wait_if_paused()`/`is_cancelled()`、5% 限频进度、`append_solution`（有序封顶 `BESQ_MAX_SOLUTIONS`，默认 32）、Tier-0/1/2 诊断计数器（`BESQ_DEEP_DIAGNOSTICS` 下热路径零开销）
- `DiagnosticsService`：单例事件循环（非阻塞 MPSC 入队 + 后台分发/落盘），`IAlgorithmObserver::create<T>()` 自动挂接/摘除

### 5.2 对外接口面

| 面 | 入口 | 说明 |
|---|---|---|
| CLI | `besq` | 28 选项（v2 模板解析器 `CLIParser`，编译期 OptionTable 校验、`duplicate_option` 诊断、help 分组、i18n 错误） |
| C ABI | `include/besq/besq.h` | 29 个 `besq_*` 函数；错误 = last_error + -1/nullptr；JSON 交换 |
| HTTP | `besq-gui` | 8 控制器 27+ 路由（见下表）；错误信封 `{ok, error}` |
| SSE | `besq-gui` | `event: progress/diag/completed/failed + data: JSON`；15s 心跳；logs 流为纯 data 帧 |
| 插件 | `plugins/` | 单 C 符号 `besq_create_algorithm`；共享 vtable/heap 故无需 destroy；加载前静态审计（W^X/危险导入） |

**Web 端点速查**：

| 控制器 | 端点（节选） |
|---|---|
| Health | `GET /health` |
| Status | `GET /api/status` |
| Settings | `GET/PATCH /api/settings`（持久化到 `config.json`） |
| Profiles | `/api/profiles` 全套 CRUD + activate/fork/merge/publish/rename + `{key}/enchantments|equipments|tags` + `{key}/enchantables/{item}`（26 条） |
| Algorithm | `GET /api/algorithms[/{name}]`、`POST .../load|unload` |
| Calculator | `POST /api/tasks`、`GET/DELETE /api/tasks/{id}`、`POST .../pause|resume`、`GET /api/tasks/{id}/events`（SSE） |
| Fs | `GET /api/fs/list`（目录选择器，越界防护） |
| Logs | `GET /api/logs`、`GET /api/logs/events`（增量日志尾） |

---

## 6. 数据流

### 6.1 CLI 求解路径

```
main.cpp → apply_lang(在 parse 之前，保证错误消息语言正确) → CLIApp::run
  → parse（v2 CLIParser）→ --algo-dir 插件加载 → load_profiles + activate
  → build_solve_request（inventory 两阶段解析 / direct 默认 dp_merge）
  → BesqContext::solve → SolvePipeline::run
      stage_apply:    CompactAdapter::apply(Profile + TagResolver → AlgorithmInput)
      stage_execute:  loader.create_executor(name) → simulate 闸门 → start → wait
      stage_recall:   CompactAdapter::recall(→ Solution[])
  → OutputFormatter: verbose / compact / json(schema v1.1)
```

### 6.2 GUI 求解路径（HTTP + SSE）

```
POST /api/tasks → Connection（增量解析、请求走私防护）→ Router
  → CalculatorController::submit → WebSolveService::start
      （单活动槽 409 TASK_ACTIVE；202 + Location）
GET /api/tasks/{id}/events → SseHub.subscribe(replay_last=true) + 立即回放 progress 帧
Worker 线程：持 _ctx_gate（串行化 ProfileManager 有效视图缓存访问）
  → 200ms 采样线程 → WebDiagObserver attach → BesqContext::solve → format
  → 发布 completed/failed 帧 → SseHub（锁外回调）→ Reactor 帧汇（weak_ptr 捕获）
      → 连接归属的 loop 线程 push_sse_frame → sock_send_nb
```

**并发不变量**（Web 层）：连接零锁（每连接终生归属一个 loop 线程）；socket close 唯一归属 poller 延迟队列（select 快照 fd 永不提前关闭）；`SseHub` 锁外回调 + 每任务最后一帧重放（迟到订阅者仍拿到终态帧）。

### 6.3 沙箱路径

```
AlgorithmLoader::create_executor → SandboxedExecutor（父进程绝不 dlopen；W^X + 危险符号审计先行）
  → spawn besq-worker（Linux: fork+socketpair+setpgid+PDEATHSIG；Win: CreateProcess+Job Object，
      KILL_ON_JOB_CLOSE + 512MB 内存/进程数上限）
  → 帧协议 [4B len][4B type][payload]；>16MiB 载荷透明分块（1MiB/帧）
  → worker 内：dlopen（先）→ seccomp（后，仅 Linux：文件/网络/进程 syscall EPERM、mprotect(EXEC) KILL；
      Windows 无 seccomp，资源约束由 Job Object 承担）
      → 真 AlgorithmExecutor；MsgRun/MsgPause(→MsgPauseAck)/MsgSerializeState(→MsgCheckpoint)
```

### 6.4 数据加载路径

```
vanilla.json / CSV / datapack(pack.mcmeta) → FormatDetector → 三解析器 → DTO
  → RegistryLoader::resolve_own_content（两阶段）→ LimitedLevelCalculator
  → Profile → ProfileManager（事务 + 快照）→ resolve_effective（依赖拓扑合并）
  → CompactAdapter::apply → EnchReg → 算法求解 → recall → Solution → 格式化/前端
```

---

## 7. 设计原则（为什么长这样）

1. **四域单向分层**（红线表 §2）——依赖方向可读，域可独立测试、独立链接（`besq-algo-core` 单文件即可复用到 worker）
2. **紧凑值类型 + 编译期契约**——热路径无堆分配、无虚函数、`static_assert` 锁死布局；序列化紧凑（线上格式为内存布局的裁剪子集，如 EnchSet 88B → 线上 72B）
3. **Profile 一等公民**——注册表三件套 + 依赖图 + 有效视图作为一个整体流转，杜绝"裸注册表 + 隐式全局状态"
4. **无状态管线模式**——`struct XxxPipeline { static run(...); }`，无注册无虚分派；`BesqContext`/`main` 用 switch 分发
5. **异步执行器 + 暂停确认握手**——算法可在任意检查点响应暂停/取消；checkpoint 只在静止点生成（可跨进程/机器恢复）
6. **沙箱缝在 executor 之上**——父进程拿到的仍是 `IExecutor`，跨进程 pause/checkpoint 语义与进程内一致；调用方不知来源
7. **诊断事件驱动 + 性能 Tier 分层**——算法线程零锁非阻塞入队；计数器按 Tier 分级，热路径默认零成本
8. **数据驱动一切**——魔咒/装备/tag 全部来自数据文件；datapack 可作为 Profile 加载；算法通过 `AlgorithmRegistry` 字符串工厂注册
9. **可复用库形态**——`besq-http`（自研 HTTP 服务器）、`common-cli`（解析器）、`common/ds`（header-only schema 引擎）都是零业务依赖的独立单元
10. **并发不变量显式化**——连接零锁、关闭唯一归属、锁外回调、`_ctx_gate` 串行化——每个共享状态都有明确的拥有者
11. **i18n 全链路**——用户可见输出全部 `tr()`；机器格式（compact/json）与日志不本地化；`--lang` > `BESQ_LANG` > 系统 locale

---

## 8. 常见任务指引

| 我想… | 怎么做 |
|---|---|
| 新增一种算法 | 实现 `IAlgorithm`（`name/evaluate/execute/get_forge_engine`），放 `src/domain/algorithm/_strategies/<目录名>/`（CMake glob 自动注册）或 `plugins/<目录名>/`（`BESQ_PLUGIN_ENTRY` 宏 + 独立构建） |
| 新增一种数据格式 | 业务域实现解析器（产出 `EnchantmentData/EquipmentData` DTO）+ 在 `FormatDetector` 注册 |
| 新增一个 HTTP 端点 | 在 `src/domain/interface/web/controllers/` 仿照现有控制器写 `BESQ_ROUTE`，注册到 `WebModule::Impl` 构造 |
| 调试算法问题 | `besq --verbose` + `--format json` 看诊断；`logs/diag/*.log` 落盘 KV；`BESQ_DEEP_DIAGNOSTICS=ON` 构建开计数器；`pause → serialize_state → start(checkpoint)` 断点续跑 |
| 跑插件 | 先 `cmake --build build`，再 `cmake -S plugins -B build/plugins -DCMAKE_PREFIX_PATH=$PWD/build && cmake --build build/plugins`，然后 `--algo-dir build/plugins`；`BESQ_SANDBOX=1` 走沙箱 |
| 改锻造成本模型 | 继承 `IForgeEngine` 只覆写所需子操作（`ForgeEngine` 尊重 `ForgeConfig` 标志：平台/忽略惩罚/忽略修复/忽略不兼容） |

---

## 9. 文档导航

| 文档 | 内容 |
|---|---|
| `docs/architecture-overview.md`（本文） | 全局架构、数据模型、接口、数据流、设计原则 |
| `docs/project-design.md` | 设计理念详述（双层类型系统、数据所有权、Pipeline 模式…） |
| `docs/domain_designs/` | 各域详细设计：business / interface / orchestration / plugin-sandbox |
| `docs/algotithm_designs/` | 算法设计：hamming、search-config-semantics、diagnostics-spec、讨论 |
| `docs/component_designs/` | 组件设计：MPMCQueue、http 中间件提案 |
| `docs/mc/anvil-mechanics-reference.md` | Minecraft 铁砧机制参考（成本公式） |
| `docs/json-output-schema.md` | JSON 输出线格式规范（v1.1） |
| `docs/软件需求规格说明书.md` | 需求规格（SRS） |

---

## 10. 测试与基准

- **框架**：`tests/framework/test_framework.h`——`TEST_CASE("name")` 自动注册 + 共享 main + per-case 超时（默认 30s，OS 杀线程）+ `SKIP`；参数 `--list/--filter/--repeat/--verbose/--timeout`
- **组织**：`tests/common`、`tests/domain/{algorithm,business,interface,orchestration}`、`tests/integration`（真实 socket e2e + SSE 线上帧）、`tests/system`（真实 CLI 二进制）
- **插件测试**：插件源码直接编入测试可执行（无需 .dll）；`test_sandbox`/`test_system_cli` 需插件树 + worker，缺失自 SKIP
- **基准**：`forge_benchmark`（harness v2 二维表 + `--json` 机器输出 + 分组/吞吐/对比）、`forge_engine_benchmark`、队列/事件循环/线程池微基准

---

## 11. 给新开发者的第一课

1. 先跑通 §1 的三个命令，用 `--verbose` 看一次完整求解输出；
2. 从 `besq --target ... --source ...` 出发，跟踪 `CLIApp::run()` → `BesqContext::solve()` → `SolvePipeline::run()` 三阶段，理解"业务 → 紧凑 → 业务"的两次转身；
3. 打开 `CompactAdapter::apply()/recall()` 对比两侧类型——这是理解双层类型系统的钥匙；
4. 再看 `docs/project-design.md` 的设计理念，最后按需深入 `docs/domain_designs/`。
