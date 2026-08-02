# 诊断子系统（`src/domain/algorithm/diagnostics/`）

## 事件链路

```
算法线程                          EventLoop 后台线程
─────────                       ─────────────────
ctx.report_progress(pct, st)    idle (atomic::wait)
ctx.report_solution(steps)
  │
  └─ DiagnosticsService::push()          唤醒
       ├─ try_post              →  try_pop → operator()
       └─ try_post_emplace                    │
                  (placement new              │
                   到队列 slot)               ▼
                                       DiagnosticsHandler
                                         ├─ 快照 observer 列表
                                         ├─ 按 event.kind 派发
                                         │   ├─ Progress → on_progress
                                         │   ├─ Solution → on_solution_found
                                         │   ├─ StateChange → on_state_changed
                                         │   └─ Exit → 文件写 + on_diagnostic + on_completed
                                         └─ _processed++
```

## DiagnosticsEvent 种类

| Kind | 触发点 | Payload |
|---|---|---|
| `Exit` | `Executor::_finalize()` | `ExitPayload`（diagnostics + output + 原子计数器） |
| `Progress` | `ctx.report_progress()` | `ProgressPayload`（pct uint8_t + status） |
| `Solution` | `ctx.report_solution()` | `SolutionPayload`（shared_ptr\<const EnchSolution\>） |
| `StateChange` | `Executor::_set_state()` / `cancel()` | `StatePayload`（prev + curr） |

## Observer 接口

```cpp
class IAlgorithmObserver {
public:
    // 按 task_id 过滤（默认接受全部）
    virtual bool accept_task_id(size_t) const { return true; }

    virtual void on_progress(size_t task_id, uint8_t pct, ProgressStatus);
    virtual void on_solution_found(size_t task_id, const vector<EnchStep>&);
    virtual void on_state_changed(size_t task_id, AlgorithmState prev, AlgorithmState curr);
    virtual void on_diagnostic(size_t task_id, const DiagnosticInfo&);
    virtual void on_completed(size_t task_id, const AlgorithmOutput&);
};
```

注册：`DiagnosticsService::instance().attach_observer(shared_ptr<IAlgorithmObserver>)`

## 诊断文件

每次算法执行在 `logs/diag/<algorithm>_<timestamp>_<random>.log` 生成一个 KV 文件：

```
# <algorithm> Exit Diagnostics
algorithm=<name>
status=Complete|NoSolution|Cancelled|Failed
wall_ms=<ms>
solution_cost=<cost>
<key>=<value>
```

**省略规则**（只输出有意义/已上报的字段，默认构建零噪声）：
- `solutions_found` / `max_depth`：仅在算法显式上报（≥0）时输出；`-1`（未跟踪，如 DP）省略。
- `dp_pass_b_ran`：仅在 Pass B 实际运行时输出 `=1`；默认未运行省略。
- `nodes_visited` / `nodes_pruned` / `steps_forged`：Tier-2 每操作计数器，`BESQ_DEEP_DIAGNOSTICS` 关闭时编译为空（恒 0），且非搜索算法不使用——三者全为 0 时整组省略。

最多保留 128 份最新文件，超出自动清理。

## 性能保证

| 路径 | 算法线程开销 | 说明 |
|---|---|---|
| `incr_nodes_visited/pruned/forged` | ~5 ns | atomic relaxd store |
| `report_progress` | ~5 ns | 未达 5% 阈值时 early return |
| `report_solution` (lvalue) | 1 次 vector copy | `const&` 路径不可避免 |
| `report_solution` (rvalue) | 0 次 copy | `&&` 路径直接 move |
| `set_exit_diagnostics` | 一次 unique_ptr move | 无字符串格式化 |
| `to_string` / 文件 I/O | **零** | 全部在 EventLoop 后台线程 |
