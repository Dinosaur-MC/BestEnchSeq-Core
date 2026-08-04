#pragma once
#include "domain/interface/web/WebSchema.h"
#include "domain/orchestration/types/SolveResult.h"
#include "domain/interface/web/WebHttpError.h"
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <memory>

class BesqContext;

namespace webhttp {

enum class TaskState { Running, Completed, Failed, Cancelled };

/// Polled snapshot of one solve task.
struct TaskStatus {
    TaskState state = TaskState::Running;
    double progress = 0.0;      // 0..1 while Running
    std::string result;         // formatted JSON (mode-appropriate), when Completed
    std::string error;          // human message when Failed
    int64_t task_id = 0;
};

/// Async solve service: one worker thread runs the (synchronous)
/// BesqContext::solve() per task. Single active slot — starting a new solve
/// while one runs throws WebHttpError(409). Mirrors the core's single
/// active_executor semantics so cancel/progress stay well-defined.
class WebSolveService {
public:
    explicit WebSolveService(BesqContext& ctx);
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
        double progress = 0.0;
        std::string result;
        std::string error;
        std::thread worker;   // owns a shared_ptr<Task> copy; joined by the
                              // destructor so no worker outlives *this/_ctx
        std::mutex mutex;
    };

    BesqContext& _ctx;
    mutable std::mutex _tasks_mutex;  // mutable: has_active() is const
    std::unordered_map<std::string, std::shared_ptr<Task>> _tasks;
    /// Serializes the blocking solve+format on the shared BesqContext.
    /// BesqContext::solve()/format() rebuild the profile's effective-view cache
    /// (ProfileManager::resolve_effective mutates a mutable cache), so two
    /// solves may never overlap — the gate enforces that on top of the
    /// 409 single-active-slot check.
    std::mutex _solve_mutex;
    int64_t _next_id = 0;
};

} // namespace webhttp
