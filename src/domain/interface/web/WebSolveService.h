#pragma once
#include "domain/interface/web/WebSchema.h"
#include "common/io/json.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class BesqContext;

namespace web {
class SseHub;

enum class TaskState : uint8_t { Running, Paused, Completed, Failed, Cancelled };

/// Polled snapshot of one solve task.
struct TaskStatus {
    TaskState state = TaskState::Running;
    double progress = 0.0;      // 0..1 while Running
    std::string result;         // formatted JSON (mode-appropriate), when Completed
    std::string error;          // human message when Failed
    int64_t task_id = 0;
    /// 已产生的算法诊断事件（紧凑 JSON，上限 500，超出丢最旧）。
    std::vector<Json> diagnostics;
    /// exit 事件的结构化 KV（{"kind":"exit",...}）；尚未产生时为 null。
    Json diag_exit = Json::null();
};

/// Async solve service: one worker thread runs the (synchronous)
/// BesqContext::solve() per task. Single active slot — starting a new solve
/// while one runs throws WebHttpError(409). Mirrors the core's single
/// active_executor semantics so cancel/progress stay well-defined.
/// The BesqContext must outlive this service; workers hold a reference to it.
///
/// `ctx_gate` is the web-layer context gate (owned by WebModule). The solve
/// worker holds it for its ENTIRE `_ctx` access window (build_request + solve
/// + format), and WebModule's profile routes hold it around every ApiProfiles
/// call. It therefore serializes a running solve against profile mutations
/// arriving on the server thread — both sides mutate ProfileManager's
/// unlocked effective-view cache via resolve_effective(), so overlapping
/// access would be a data race.
class WebSolveService {
public:
    /// `hub` is an optional SSE frame sink (Task 14: SseHub). Null keeps the
    /// pre-SSE behavior (polling snapshot via status()) — the 2-arg form is
    /// still supported for backward compatibility.
    explicit WebSolveService(BesqContext& ctx, std::mutex& ctx_gate,
                             web::SseHub* hub = nullptr);
    ~WebSolveService();

    WebSolveService(const WebSolveService&) = delete;
    WebSolveService& operator=(const WebSolveService&) = delete;

    /// Build the SolveRequest from a WebTaskDto and run it on a worker thread.
    /// Returns the task id. Throws WebHttpError(409) when a task is active.
    std::string start(const WebTaskDto& dto);

    /// Snapshot a task. Throws WebHttpError(404) when unknown.
    TaskStatus status(const std::string& id);

    /// Cancel the active task (no-op when the task already finished).
    bool cancel(const std::string& id);

    /// Pause the running task (batch C): the executor quiesces at its next
    /// pause point and the task state flips to Paused. Throws WebHttpError(404)
    /// for an unknown task and WebHttpError(409, TASK_NOT_PAUSABLE) when the
    /// task is not Running. A pause that races the solve's publish window (the
    /// executor handle is not yet published) is a lost no-op — the task simply
    /// completes instead (single-slot semantics stay intact either way).
    bool pause(const std::string& id);

    /// Resume a paused task: the executor continues and the task state flips
    /// back to Running. Throws WebHttpError(404) for an unknown task and
    /// WebHttpError(409, TASK_NOT_RESUMABLE) when the task is not Paused.
    bool resume(const std::string& id);

    /// True while any task is Running or Paused (drives /api/status + 409).
    /// A paused task still occupies the single active slot — the executor is
    /// live and blocked at a pause point, so a new solve must not start.
    bool has_active() const;

private:
    struct Task {
        std::string id;
        int64_t numeric_id = 0;
        /// 原子状态字：状态机转移用 CAS（cancel/pause/resume/worker 终态提交），
        /// 读方 acquire 快照；终态（Completed/Failed）一经提交不被覆盖。
        std::atomic<TaskState> state{TaskState::Running};
        /// 结果/错误：原子共享指针交换（写方 store、读方拷贝快照，无锁读）。
        /// 写序：payload 先（release）、state 后（release）——状态可观察时
        /// 载荷必已可见。
        std::atomic<std::shared_ptr<const std::string>> result{nullptr};
        std::atomic<std::shared_ptr<const std::string>> error{nullptr};
        /// 算法诊断事件流（WebDiagObserver 转出的紧凑 JSON；worker 回调写入，
        /// 上限 500，超出丢最旧）。仅 worker 写、status() 拷贝快照——
        /// task->mutex 只护这两者。
        std::vector<Json> diagnostics;
        /// exit 事件的结构化 KV（{"kind":"exit",...}）；尚未产生时为 Json::null()。
        /// task->mutex 保护（同 diagnostics）。
        Json diag_exit = Json::null();
        /// Set (release) as the worker thread's very last action, after every
        /// access to *this/_ctx. Gates task reaping: a terminal task may only
        /// be erased from the table once its worker has fully exited (and been
        /// joined) — never while the worker still runs, and never by destroying
        /// a still-joinable thread (which would std::terminate).
        std::atomic<bool> finished{false};
        /// The worker holds a shared_ptr<Task> copy; joined by the destructor
        /// (or by the reap loop before erase) so no worker outlives *this/_ctx.
        std::thread worker;
        /// 仅护 diagnostics/diag_exit 快照（其余字段已原子化）。
        std::mutex mutex;
    };

    BesqContext& _ctx;
    std::mutex& _ctx_gate;       // web-layer gate, shared with WebModule
    web::SseHub* _hub;           // optional SSE frame sink; null = polling only
    mutable std::mutex _tasks_mutex;  // mutable: has_active() is const
    std::unordered_map<std::string, std::shared_ptr<Task>> _tasks;
    int64_t _next_id = 0;
};

} // namespace web
