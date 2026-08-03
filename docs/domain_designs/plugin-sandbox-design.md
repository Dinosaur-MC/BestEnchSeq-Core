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
算法内核  besq-algo-core（SHARED）—— 算法域 + 插件框架
BESQ核心  besq-core（INTERFACE）—— 算法域 + 业务域 + 编排域 + 内嵌数据，排除接口域
CLI       besq（EXE）—— 链 besq-core + besq-domain-interface
沙箱      besq-worker（EXE）—— 链 besq-algo-core + worker 源
纯净扩展  algo_*（SHARED）—— 链 besq-algo-core（同一份共享内核）
（besq-minimal / besq-builtin 已删除——builtin 并入 besq-core）
```

### 3.2 单例统一：共享内核

决策 B（诊断转发）依赖 `DiagnosticsService::instance()` 是**同一单例**。算法内核是 **SHARED**，父进程（via besq-core）、沙箱 worker、插件**三方链同一份 `libbesq-algo-core.so`** → 符号从该共享库解析，单例统一：

```
此前尝试：静态内核 + 插件裸 + 宿主 -rdynamic
  → 小 worker（符号少）能 dlopen 裸插件
  → 大父进程 besq 的 -rdynamic 导出表导致动态链接器重定位 SEGV（strace 实证）
修正：内核 SHARED，三方共享 → 无 -rdynamic，父/worker 都能 dlopen 插件，单例统一 ✅
```

### 3.3 构建方式

```cmake
# 算法域（src/domain/algorithm/CMakeLists.txt）
add_library(besq-algo-core SHARED <算法域源文件 + 插件框架 + 策略>)
# 核心（根 CMakeLists）——编译内嵌数据 + 链三域
add_library(besq-core STATIC <builtin/*.cpp + embedded>)  # 现在是 INTERFACE 聚合
target_link_libraries(besq-core PUBLIC orchestration business algo-core)
# 宿主无 -rdynamic（插件从 libbesq-algo-core.so 解析）
add_executable(besq src/main.cpp)        # 链 besq-core + interface
add_executable(besq-worker src/worker/main.cpp)  # 链 besq-algo-core（共享）
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
分块：≤16MiB 单帧直传，>16MiB 自动拆 1MiB 帧（MsgChunkedStart/Data/End）透明重组
     —— MB~GB 级 checkpoint 无需单独分块协议

主→子（请求）：
  MsgGetName / MsgGetVersion / MsgGetMode / MsgIsSerializable   构造握手（元数据缓存）
  MsgEvaluate / MsgSimulate                                     预检（Windows 二进制冒烟用 evaluate）
  MsgRun(AlgorithmInput)                                        运行新求解
  MsgResumeRun(不透明 checkpoint blob)                          恢复运行（input 内嵌在 blob）
  MsgPause / MsgResume / MsgCancel                              运行期控制
  MsgSerializeState                                             暂停时请求 checkpoint blob

子→主（响应 + 异步事件）：
  MsgResult     —— 按请求类型：元数据值 / checkpoint blob / 最终 AlgorithmOutput（编码）
  MsgProgress / MsgSolution   流式事件
  MsgError
```

序列化复用 `ByteStream`（`besq-common-io`）。`AlgorithmOutput` 有专用编解码
（`encode_algorithm_output`/`decode_algorithm_output`）——`solutions` 含 `std::vector`
非平凡类型，逐元素 `serialize()` 手写，避免 ByteStream 的平凡类型模板。

## 6. Executor 代理面（IExecutor）—— 沙箱缝在 executor 之上

**`AlgorithmExecutor` 是整个算法域的权威入口**：它持有 `ExecutionContext`、驱动
`IAlgorithm::execute()`、跑状态机（Idle→Running→Paused→Completed/Failed/Cancelled）、
产出/消费 checkpoint。编排层（`SolvePipeline`）只跟它打交道。

因此沙箱缝**不在 `IAlgorithm`**（那样会把 executor/context/algorithm 的高度耦合拆散跨进程、
再用握手重新接通——旧 `SandboxedAlgorithm` 的教训），而在 **executor 之上**：

```
父进程 SandboxedExecutor : IExecutor          worker 进程（真 AlgorithmExecutor）
  name/version/supported_mode/simulate          ├─ ctx（内部，本地，不拆）
  start(input)  ── MsgRun ────────────▶         ├─ algorithm（内部，本地）
  start(blob)   ── MsgResumeRun ──────▶         ├─ serialize_state（本地）
  pause/resume/cancel（任意线程）──▶ exec.pause()/resume()/cancel()（原生线程安全）
  serialize_state() ── MsgSerializeState ─▶    exec.serialize_state() → 完整不透明 blob
  wait()/output() ◀── MsgResult(AlgorithmOutput)
  （reader 线程：运行期唯一读管道者）
```

IExecutor 公共面（`src/domain/algorithm/IExecutor.h`）：`name/version/supported_mode/simulate`
+ `start(input)`/`start(checkpoint)` + `pause()/resume()/cancel()` + `wait()/state()/progress()/output()`
+ `serialize_state()/is_serializable()`。`AlgorithmExecutor` 与 `SandboxedExecutor` 并列实现之；
`SolvePipeline`/`BesqContext` 只依赖 `IExecutor`，沙箱开关在 `AlgorithmLoader::create_executor()`：
沙箱插件 → `SandboxedExecutor`，内建/无沙箱插件 → `AlgorithmExecutor`（**内建策略永不沙箱化**——
编译进可信内核）。

对比旧实现（已删除 `SandboxedAlgorithm`）：IAlgorithm 代理被迫
- 把 ctx 劈成父侧壳 + worker 侧真身，progress/solution 跨进程回注
- pause/cancel 退化为 `notifier → MsgPause → worker 控制线程 → 本地 ctx` 四段接力
- 序列化靠 `_pipe_yielded` 原子握手 + `SandboxSerializer` 代理 + section 编解码，硬耦合"已暂停"语义

全部删掉。IPC 上只留粗消息，executor 的耦合完整留在 worker 内，checkpoint 变为**不透明 blob**
（`exec.serialize_state()` 产出含 input 段的完整 checkpoint，父侧只搬字节）。

## 7. 诊断转发

```
worker：
  DiagnosticsService::instance().set_persist(false)          # 不写盘
  attach_observer(IpcForwardObserver)                        # 运行期唯一 observer
  worker 侧所有 stdout 写（observer 事件 + 控制线程序列化回复 + serve 的 MsgResult）
  统一走 IpcForwardObserver::send_frame() 的锁 → 帧永不交错

  algorithm → ctx.report_progress()/report_solution()
           → 子进程 DiagnosticsService → IpcForwardObserver
           → MsgProgress/MsgSolution 帧

父侧 SandboxedExecutor（reader 线程）：
  MsgProgress → 更新 progress()；MsgSolution → 忽略（权威解法在最终 MsgResult 的
  AlgorithmOutput 里）；MsgResult → 解码存 _output。CLI 无 observer 依赖父进程
  DiagnosticsService，无回注需求。
```

## 8. 生命周期与故障处理

```
一个 SandboxedExecutor = 一个 worker 进程：构造时 spawn + 元数据握手（name/version/mode/
is_serializable），析构时 cancel + join reader + kill。同一 worker 可复用多次求解
（AlgorithmExecutor 允许从 Completed 重跑）。CLI 单次求解场景最简单。

运行期 worker 结构：
  serve 线程：空闲时处理元数据/预检消息；收到 MsgRun/MsgResumeRun 后阻塞在 exec.wait()
  控制线程：运行期唯一 stdin 读者，事件驱动（poll/eventfd 或 PeekNamedPipe+事件），
            把 MsgPause/Resume/Cancel/SerializeState 直接调 executor 的原生线程安全 API——
            不再有"转发给本地 ctx"的接力

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
已固化（2026-08-03 架构重定后）：
  test_ipc_protocol（跨平台）——帧往返/空帧/多字节帧/多帧顺序 + AlgorithmOutput 编解码回环，31 断言
  test_sandbox（双平台）——worker 存活 + 元数据；Windows evaluate(0x1A) 二进制 IPC 冒烟；
    Linux malicious 插件 OPEN BLOCKED 断言（seccomp EPERM）；
    pause/resume 经 SandboxedExecutor 接口端到端；checkpoint 往返（暂停→序列化 blob→新 worker
    恢复→Completed+解）；9 断言（Windows）/ 10 断言（WSL，多 seccomp 一项）
  67/67 全量双平台

待补（M3/M4）：
  崩溃插件（segfault）→ "plugin crashed"
  std::thread 插件 → 验证 CLONE_THREAD 放行
  Capability profile 强制 → 声明 None 却联网 → 被禁
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

M1 Linux 隔离 + IPC（已实现 + WSL 验证通过）：
  - IpcProtocol（帧协议：len+type+payload）
  - besq-worker 服务循环（stdin/stdout IPC，事件流 + 控制消息）
  - seccomp 白名单（dlopen 后安装；EPERM 拦截文件/网络，KILL 拦截 mprotect(PROT_EXEC)）
  - SandboxedAlgorithm（父进程 IAlgorithm 代理：spawn worker + IPC + 事件注入父 ctx + 取消轮询）—— 2026-08-03 已被 executor 级架构取代（见 M2 架构修正）
  - 集成：AlgorithmLoader.set_sandbox_enabled()（BESQ_SANDBOX=1 opt-in）
  - ✅ WSL 验证：
    * malicious 插件 in-process OPEN OK / sandboxed OPEN BLOCKED（EPERM）
    * idastar 经沙箱 worker 完整求解（解法流回父进程，50ms）
  - 关键修复：沙箱模式父进程不 dlopen 插件；seccomp BPF 两处 bug
    （arch jt/jf 方向；bpf_ld_abs 丢弃 offset 导致全 KILL——真正根因）
  - 测试插件 plugins/malicious/（open("/etc/passwd") 对照），可作 M3 自动化用例

M2 Windows（已实现 + Windows 验证）：
  - SandboxedAlgorithm Windows spawn：CreateProcess + 双管道（父→子 stdin、子→父 stdout）+ stderr 管道
  - Job Object：KILL_ON_JOB_CLOSE + 512MB 进程/作业内存上限 + 单活动进程
  - worker 服务循环在 Windows 复用（_read/_write + 二进制管道）
  - BESQ_SANDBOX=1 在 Windows 也启用（get_env<bool>）
  - ✅ Windows 验证：idastar 经沙箱求解；无 seccomp（Windows 无等价物）——隔离靠进程 + Job Object 资源上限；文件/网络阻断需 AppContainer（后续）
  - ✅ 取消/超时事件驱动化（2026-08-03）：`ExecutionContext::cancel()` 增加零成本通知钩子
    （冷路径，进程内算法仅多一次 null 原子 load；字段追加在类**末尾**，保持既有成员偏移不变），
    父进程 `execute()` 与 worker 控制线程改为事件驱动等待——取消/超时即时生效（cancel→join 0ms），
    不再有 100ms 轮询延迟 / Windows 纯阻塞读失效问题
  - ✅ pause/resume 接通（2026-08-03）：`ExecutionContext` 的 notifier 泛化为**控制态 notifier**
    （cancel/pause/resume 都触发同一个 eventfd/事件），父进程 execute() 循环在唤醒时 diff 当前
    暂停态并转发 MsgPause/MsgResume；worker 控制线程本就能处理，插件算法均调用 `wait_if_paused()`。
    取消钩子同时改为单个 `std::atomic<std::shared_ptr<ControlNotifier>>`——修掉每次 execute 泄漏
    一个 fd/句柄 + fn/ud 撕裂读（曾向 Linux stdin fd 0 写字节）
  - ✅ 审计 RED 清单补全（2026-08-03）：文件打开入口（fopen/fopen_s/_wfopen）、网络解析
    （getaddrinfo/gethostbyname 等）、代码/进程注入（CreateRemoteThread/WriteProcessMemory/
    memfd_create/process_vm_writev/bpf）、Windows 注册表/进程控制、Linux syscall/prctl/unshare；
    非沙箱模式下这些危险导入一律硬拒
  - ✅ checkpoint 序列化/恢复接通（2026-08-03 重构后）：沙箱缝在 executor 之上，checkpoint
    走 worker 内真 executor 的 `serialize_state()`/`start(checkpoint)`——**完整不透明 blob**
    （含 input 段），父侧 `SandboxedExecutor` 只搬字节。**透明分块传输**：`write_frame`/
    `read_frame` 自动把 >16MiB 载荷拆成 1MiB 帧并重组，支持 MB~GB 级 checkpoint。worker 控制
    线程把 MsgSerializeState 转调 `exec.serialize_state()`；`MsgResumeRun(blob)` 恢复运行。
    无 `_pipe_yielded` 握手（executor 状态静止契约保证，同 in-process）。**修复 AStar 两处恢复
    bug**：(1) open heap 从局部 move 改为成员 `OpenSet`；(2) restored 路径补 `_budget` 初始化。
    双平台 test_sandbox 验证：暂停→序列化（Windows 2MB / WSL 173KB）→新 worker 恢复→求解成功

  - ✅ 架构修正（2026-08-03）：沙箱缝从 **IAlgorithm** 上移到 **executor**——
    AlgorithmExecutor 是算法域的权威入口（持有 ctx/状态机/序列化），拿 IAlgorithm 做代理面
    会把 executor/algorithm/context 拆散跨进程再手工接回（`_pipe_yielded` 握手、notifier、
    代理序列化器、section 编解码）。重做为 `IExecutor` 接口 + `SandboxedExecutor`（父侧粗消息
    代理）+ worker host 真 `AlgorithmExecutor`；`SolvePipeline`/`BesqContext` 只依赖 `IExecutor`。
    删除 `SandboxedAlgorithm`、`SandboxSerializer`、`ExecuteControlNotifier`、`encode/decode_sections`、
    `MsgDeserializeState`。`ExecutionContext` 随后**恢复沙箱前纯本地形态**——`ControlNotifier`/
    `set_control_notifier()` 及三个 notify 钩子整体删除（executor 级沙箱已无人安装它），字段是末尾
    成员、删除不移动任何既有偏移，但 idastar 插件内联调 `ctx.cancel()` 烧过该偏移 → **必须连插件
    一起重编**（文档教训 2 的既有规则）。重编后双平台 67/67 验证通过。

M3 Capability profile 分级 + 故障处理 + 全套测试

M4（可选）：
  - besq audit-plugin <path> CLI
  - 哈希白名单（仅对用户手动锁定的版本）
  - Landlock 文件路径限制 / Windows AppContainer
```

### 两个平台教训（2026-08-03 实证）

**1. Windows 匿名管道不是可靠等待对象**

`WaitForMultipleObjects` 等待匿名管道读句柄会在**空管道上虚假报告"可读"**，随后阻塞
`read_frame` 永久挂死（worker 的 `control.join()` 死锁 → 不响应 → 父进程挂死）。
修复：数据侧用 `PeekNamedPipe`（非阻塞探测）+ 取消/退出用**真正的等待对象**（Win32 事件），
事件即时唤醒，数据探测 ≤1ms。Linux 无此问题——`poll` 在管道 fd 上可靠，用 `eventfd` + `poll(-1)`
保持完全事件驱动。

**2. 头文件布局变化会打断插件 ABI**

`ExecutionContext` 新增字段若插在既有成员**中间**，会把后续成员偏移整体后移。插件 DLL 若用旧
头文件编译（未随 host 重建），其内联成员函数按旧偏移读写 → 搜索时在 besq-algo-core.dll 里
段错误（0xC0000005）。恶意插件只调非内联的 `report_progress`（走 DLL 符号）所以幸存——迷惑性极强。
修复：新字段**追加在类末尾** + 重建插件树。注意：修改算法域头文件后必须重编 `build/plugins`。

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
