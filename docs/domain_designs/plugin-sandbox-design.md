# 插件沙箱隔离 — 设计文档

> 状态：**设计中**（构建基础已完成，隔离/IPC 实现待开工）
> 关联：插件安全审查（`PluginAudit`）为其前置静态层，本文档定义其运行时遏制层。

## 1. 背景与威胁模型

### 1.1 为什么需要沙箱

插件可能来自第三方社区开发者，算法迭代快。这决定了**无法依赖"预先信任"**：

| 信任分发手段 | 为何不适用 |
|-------------|-----------|
| 哈希白名单 | 插件更新频繁，白名单需持续维护，违背"下载即用" |
| 签名验证 | 社区无单一权威；自签名等于没签 |

所以安全模型从"信任闸门"转为**遏制（containment）**：审查的目标不是"这个插件可信吗"，而是"我能以最小权限运行它，并在越界时检测+隔离吗"。

### 1.2 假设

- 插件不可信（可能恶意或带 bug），来自快速迭代的社区
- 目标：限制越权造成的损害，而非判定可信

### 1.3 防护对象

| 对象 | 威胁 | 手段 |
|------|------|------|
| 宿主进程内存（注册表/诊断数据/其他插件） | 篡改/窃取 | **进程隔离**（worker 独立地址空间） |
| 文件系统 | 读写用户文件 | seccomp 拒 `open/openat`（Linux）/ Job Object + restricted token（Windows） |
| 网络 | 外传数据 | seccomp 拒 `socket/connect` / AppContainer（后续） |
| 宿主可用性 | 死循环/内存耗尽/崩溃 | timeout / Job Object 资源上限 / 进程隔离 |

### 1.4 沙箱同时防御"恶意"和"缺陷"

社区插件最常见问题其实是 bug：

| 缺陷 | 沙箱手段 |
|------|---------|
| 死循环 | 子进程 timeout，kill |
| 内存爆炸 | RLIMIT_AS（Linux）/ Job Object 内存上限（Windows） |
| 崩溃拖垮宿主 | 子进程隔离，`waitpid` 捕获信号 |
| 插件间污染 | 每插件独立子进程 |

## 2. 总体架构

### 2.1 核心决策：整个算法域移入沙箱

**`ExecutionContext` 的热路径绝不能跨进程**（量化）：

| 调用 | 频率 | in-process | IPC 化 |
|------|------|-----------|--------|
| `incr_nodes_visited()` | 每次展开 | ~1 ns | 2–5 µs（1000× 退化） |
| `is_cancelled()` | 每 N 步 | ~1 ns | 2–5 µs |
| `report_progress()` | 5% 限频 | 异步 | 尚可（低频） |

结论：**把整个求解引擎（算法域）移入 worker 进程**，`ExecutionContext` 完全留在 worker 内。IPC 边界恰好落在可序列化的契约上：`AlgorithmInput → AlgorithmOutput`。

### 2.2 进程拓扑

```
┌── 父进程 besq（CLI 壳）──────────────────────────────────────┐
│ CLI → 业务域 → CompactAdapter → AlgorithmInput                │
│       → IPC request                                           │
│       ← 限频 progress/solution/exit_diag 事件                 │
│       ← AlgorithmOutput → format                              │
│ spawn/回收 besq-worker                                        │
└──────────────────────────▲─────────────────────────────────┘
                           │ stdin/stdout = IPC 管道
┌── 子进程 besq-worker ────┴─────────────────────────────────┐
│ seccomp/JobObject → dlopen(plugin)                          │
│ → 本地 ExecutionContext（热路径零 IPC）                      │
│ → 本地 DiagnosticsService + IPC 转发 observer                │
│ → AlgorithmExecutor / forge engine / resolvers 全在子进程     │
└────────────────────────────────────────────────────────────┘
```

### 2.3 边界

- **父进程**（可信，besq-core 全量）：CLI、业务域、CompactAdapter、OutputFormatter、DiagnosticsWriter、沙箱生命周期
- **子进程**（不可信，仅算法域）：dlopen 插件、执行 `IAlgorithm` 全部方法、forge engine、resolver、本地 ExecutionContext

## 3. 构建基础（2026-08-03 重构定案）

### 3.1 目标分类

```
基础设施  besq-common / besq-common-*（STATIC/INTERFACE）
算法内核  besq-algo-core（STATIC）—— 算法域 + 插件框架
BESQ核心  besq-core（STATIC）—— 算法域 + 业务域 + 编排域 + 内嵌数据，排除接口域
CLI       besq（EXE）—— 链 besq-core + besq-domain-interface
沙箱      besq-worker（EXE）—— 链 besq-algo-core + worker 源
纯净扩展  algo_*（SHARED）—— 仅依赖算法内核符号
（besq-minimal / besq-builtin 已删除——builtin 并入 besq-core）
```

### 3.2 单例统一：宿主导出 + 插件裸解析

决策 B（诊断转发）依赖 `DiagnosticsService::instance()` 是**同一单例**。算法内核是 STATIC，链接进宿主（CLI/worker），宿主用 `-rdynamic`（Linux）/ `ENABLE_EXPORTS`（Windows）导出；插件为**裸扩展**，dlopen 时符号解析到宿主的唯一一份内核：

```
Linux   → 插件链 INTERFACE（headers only），符号 undefined，dlopen 时解析到宿主 -rdynamic 导出
Windows → 插件链静态 besq-algo-core.lib（自带副本）——已知限制，待 M2 Windows 沙箱解决
```

CLI in-process 插件同样解析到 CLI 导出的内核 → 单例统一。

### 3.3 构建方式

```cmake
# 算法域（src/domain/algorithm/CMakeLists.txt）
add_library(besq-algo-core STATIC <算法域源文件 + 插件框架 + 策略>)
# 核心（根 CMakeLists）——编译内嵌数据 + 链三域
add_library(besq-core STATIC <builtin/*.cpp + embedded>)
target_link_libraries(besq-core PUBLIC orchestration business algo-core)
# 宿主导出符号
add_executable(besq src/main.cpp)        # 链 besq-core + interface
add_executable(besq-worker src/worker/main.cpp)  # 链 besq-algo-core
# 宿主 -rdynamic / ENABLE_EXPORTS
# 注意：business ↔ builtin 是真实循环静态依赖，UNIX 链接必须用
#   -Wl,--start-group besq-core besq-domain-business -Wl,--end-group
# group 必须同时包住 core 与 business——只包 core（或单独的 builtin 库）
# 会让 business 落在 group 外而失效。
```

## 4. 隔离机制

### 4.1 Linux — seccomp-bpf syscall 白名单

```
安装时机：dlopen 之后（dlopen 需要 open/mmap(PROT_EXEC) 加载 .so）

ALLOW（纯计算 + IPC）：
  mmap/munmap/mprotect(无PROT_EXEC)/brk/madvise
  read/write          # 仅限 IPC fd（arg 过滤）
  futex               # 原子同步
  clone/clone3        # 仅限 CLONE_THREAD（std::thread，bb_dp 并行需要）
  exit/exit_group/rt_sigaction/rt_sigprocmask/clock_gettime/sched_yield/nanosleep

DENY（默认 KILL）：
  open/openat/creat/unlink/rename/mkdir/chmod    # 文件系统
  socket/connect/bind/accept                     # 网络
  execve/fork/ptrace                             # 进程/调试
  mprotect(PROT_EXEC)                             # JIT/代码注入
```

关键细节：
- `clone` flag 过滤：`SCMP_CMP` 检查 `CLONE_THREAD` 位 → 允许线程、拒绝 fork
- `write` fd 参数过滤到 IPC fd
- 插件已加载的代码（.text 段）不受影响——seccomp 只拦**新**的 syscall

### 4.2 Windows — Job Object + restricted token

```
CreateProcess（restricted token：去 SeDebugPrivilege 等高权限）
  + AssignProcessToJobObject：
      - 内存上限（如 512MB）防 OOM 拖垮宿主
      - CPU 时间上限 防死循环
      - JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 宿主退出自动回收
  + SetDefaultDllDirectories(SYSTEM32) 防 DLL 劫持
  + 进程隔离（独立地址空间）→ 主要防线

AppContainer（文件/网络 ACL 级隔离）→ 后续增强
```

### 4.3 Capability → 沙箱 Profile

```
PluginCapability::None        → 完整 DENY 白名单（默认）
PluginCapability::Filesystem  → 放宽 open/openat 但限制路径（需 Landlock/LPAC，后续）
PluginCapability::Network     → 放宽 socket/connect（仅网络，仍禁文件）
PluginCapability::Unrestricted→ 不装 seccomp（仅审计 + 记录）

原则：声明什么 = 允许什么，其余物理禁止。撒谎的插件被 seccomp kill。
```

## 5. IPC 协议

```
双向 socketpair（Linux）/ 双匿名管道（Windows）
帧格式：[4B 长度][4B 消息类型][payload]

主→子（请求）：
  MSG_RESOLVE / MSG_SIMULATE / MSG_EXECUTE / MSG_PROCESS /
  MSG_GET_FORGE_ENGINE / MSG_EVALUATE / MSG_CANCEL / MSG_PAUSE / MSG_RESUME

子→主（响应 + 异步事件）：
  MSG_RESPONSE / MSG_PROGRESS / MSG_SOLUTION /
  MSG_DIAGNOSTICS / MSG_CRASH / MSG_SANDBOX_VIOLATION
```

序列化复用 `ByteStream`（`besq-common-io`）泛型 write/read。紧凑类型（`EnchSet` 位掩码 + `uint8_t[64]`、`Item` POD）天然二进制友好。

## 6. IAlgorithm 代理面（全量）

```
父进程 SandboxedAlgorithm 实现 IAlgorithm，每个方法一次 IPC 往返：
  name/version/supported_mode/is_resumable/evaluate()  → 子进程首次读取缓存
  resolve(input) → ResolverOutput
  simulate(input) → bool
  execute(input, ctx)  → 子进程执行，progress 流式回传
  process(sol, cfg, reg) → optional<Item>
  get_forge_engine()   → 父进程拿"转发型 IForgeEngine"（forge 调用走 IPC，量小）
  get_serializer()     → 沙箱插件 v1 禁用 resume（后续再做 checkpoint 跨进程）
```

## 7. 诊断转发（决策 B）

```
子进程：
  DiagnosticsService::instance().set_persist(false)          # 子进程不写盘
  attach_observer(IpcForwardObserver)                        # 唯一 observer

  algorithm → ctx.report_progress()/report_solution()
           → 子进程 DiagnosticsService → IpcForwardObserver
           → 序列化 DiagnosticsEvent → IPC 管道

父进程：
  IPC reader 线程 → 反序列化 → 父进程 DiagnosticsService::push()
                 → observers（CLI --verbose）+ DiagnosticsWriter（写盘）
```

单一写入者（父进程），无文件竞争，父进程完全控制诊断去向。

## 8. 生命周期与故障处理

```
每次 solve 惰性 spawn 一个 worker，销毁时 kill。CLI 单次求解场景最简单。

| 子进程状态     | 判定            | 上报                   |
|---------------|----------------|------------------------|
| 被 seccomp 杀 | 越权操作        | "sandbox violation" → 标记插件可疑 |
| SIGSEGV/SIGABRT | 崩溃         | "plugin crashed" → 优雅失败 |
| 超时          | 死循环          | "timed out" → kill      |
| 内存超限       | 泄漏/失控        | "OOM"                   |
| 正常退出       | 完成            | 正常输出                |
```

## 9. 性能

```
spawn worker            ≈ 10–50 ms（fork 或 exec+dlopen）
AlgorithmInput 序列化    ≈ 亚毫秒–几 ms（KB 级紧凑类型）
IPC 往返                ≈ 微秒级（本机 socketpair/管道）
progress 事件            限频 5% ≈ 20 次/solve
seccomp 过滤器           syscall 时 <1% 开销（热循环无 syscall → 零开销）
────────────────────────
总开销 < 100ms，相对秒级搜索可忽略
```

## 10. 与现有 in-process 模式共存

```
内置策略（hamming/dp_merge/bb_dp）→ 编译进 besq-algo-core，照常 in-process
外部插件 → 默认沙箱
逃生门：BESQ_PLUGIN_TRUSTED=<name> 让指定插件 in-process（调试/性能，仍过审计）
```

## 11. 测试策略

```
恶意插件（open 文件）    → seccomp kill → "sandbox violation" ✅
死循环插件              → timeout kill ✅
崩溃插件（segfault）    → "plugin crashed" ✅
std::thread 插件         → 验证 CLONE_THREAD 放行 ✅
合法插件（idastar）      → 完整功能往返 ✅
Capability profile 强制 → 声明 None 却联网 → 被禁 ✅
每个 IAlgorithm 方法 IPC 往返 → 参数/返回值 roundtrip ✅
```

## 12. 实施阶段

```
M0（已完成）构建基础：
  - besq-algo-core 算法内核（STATIC）
  - besq-core 三域聚合（INTERFACE）+ besq-builtin（内嵌数据）
  - besq-worker 目标 + 骨架（--plugin/--capability 解析 + 插件加载）
  - 插件纯净扩展（Linux 裸解析 / Windows 静态内核）
  - 预设 full/debug/minimal/cli/sandbox
  - 65 测试全通过（Windows）

M1 Linux 隔离 + IPC：
  - socketpair + besq-worker 服务循环
  - seccomp 白名单（dlopen 后安装）
  - ByteStream wire format
  - SandboxedAlgorithm（父进程 IAlgorithm 代理）

M2 Windows：
  - CreateProcess + Job Object + 双管道

M3 Capability profile 分级 + 故障处理 + 全套测试

M4（可选）：
  - besq audit-plugin <path> CLI
  - 哈希白名单（仅对用户手动锁定的版本）
  - Landlock 文件路径限制 / Windows AppContainer
```

## 13. 构建系统整理清单（已完成）

- [x] 算法域统一为 `besq-algo-core` STATIC（删除孤儿 `besq-domain-algorithm` 与 `besq-minimal`）
- [x] `besq-core` 改为 STATIC（三域 + builtin 内嵌数据，排除接口域）
- [x] CLI 链 `besq-core + interface`，`-rdynamic`/`ENABLE_EXPORTS` 导出内核
- [x] worker 链 `besq-algo-core`，同导出
- [x] 插件纯净扩展（Linux INTERFACE 裸解析 / Windows 静态内核）
- [x] `besq-coreConfig.cmake.in` 只提供 `besq-algo-core::besq-algo-core`
- [x] UNIX 链接 `--start-group` 解决 business↔builtin 循环（group 须含 core/business/orchestration/interface/algo 整条链）
- [x] 清理过时产物（besq-core.dll/besq-minimal.lib/besq-domain-algorithm.lib/besq-builtin.lib）
- [x] 预设 full/debug/minimal/cli/sandbox
- [x] Windows 65 测试全通过 + CLI/worker/插件功能验证
- [ ] **WSL 待验证**（build-wsl 全量构建 + 测试 + 插件构建）

## 附：关键代码现状

- `src/domain/algorithm/plugin/PluginAudit.*` — 前置静态审查（已完成）
- `src/worker/main.cpp` — worker 骨架（M1 待实现）
- `src/domain/algorithm/serialization/Checkpoint.*` — ByteStream 序列化基础
- `src/domain/algorithm/ExecutionContext.h` — 具体类，非虚方法，热路径
- `src/domain/algorithm/diagnostics/DiagnosticsService.h` — 支持 attach_observer + set_persist
