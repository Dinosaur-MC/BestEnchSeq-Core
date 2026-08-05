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

enum class TaskState { Running, Completed, Failed, Cancelled };

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

    /// True while any task is Running (drives /api/status + 409).
    bool has_active() const;

private:
    struct Task {
        std::string id;
        int64_t numeric_id = 0;
        TaskState state = TaskState::Running;
        std::string result;
        std::string error;
        /// 算法诊断事件流（WebDiagObserver 转出的紧凑 JSON；worker 回调写入，
        /// 上限 500，超出丢最旧）。task->mutex 保护。
        std::vector<Json> diagnostics;
        /// exit 事件的结构化 KV（{"kind":"exit",...}）；尚未产生时为 Json::null()。
        /// task->mutex 保护。
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
