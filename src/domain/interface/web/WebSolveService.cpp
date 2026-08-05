#include "WebSolveService.h"
#include "SseHub.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/Router.h"
#include "domain/orchestration/types/SolveRequest.h"
#include "common/io/json.h"
#include "common/i18n/Language.h"
#include <chrono>
#include <utility>
#include <vector>

namespace web {

namespace {

EnchSet build_source(const std::vector<InvEnchDto>& source, const EnchantmentRegistry& ench_reg) {
    EnchSet out;
    for (const auto& e : source) {
        auto it = ench_reg.find(NSID(e.id));
        if (it == ench_reg.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_ench", e.id));
        if (e.level < 1 || e.level > it->max_level)
            throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max", e.id, e.level, it->max_level));
        out.emplace(it->id, it->name, e.level);
    }
    return out;
}

Item build_target(const InvTargetDto& t, const EnchantmentRegistry& ench_reg,
                  const EquipmentRegistry& eq_reg) {
    Item item;
    if (t.item == "book" || t.item == "enchanted_book") {
        item.id = NSID("minecraft:enchanted_book");
    } else {
        auto eq_it = eq_reg.find(NSID(t.item));
        if (eq_it == eq_reg.end())
            throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", t.item));
        item.id = eq_it->id;
        item.durability = eq_it->max_durability;
    }
    item.enchantments = build_source(t.enchants, ench_reg);
    return item;
}

SolveRequest build_request(const WebTaskDto& dto, const BesqContext& ctx) {
    const auto& ench_reg = ctx.enchantments();
    const auto& eq_reg = ctx.equipment();

    SolveRequest req;
    req.target_item = build_target(dto.target, ench_reg, eq_reg);

    bool inventory = !dto.items.empty();
    req.mode = inventory ? AlgorithmMode::inventory : AlgorithmMode::direct;

    if (inventory) {
        // items → InventoryPayload (mirrors CAbiBindings::parse_inventory_payload)
        InventoryPayload payload;
        for (const auto& it : dto.items) {
            EnchSet ench_set = build_source(it.enchants, ench_reg);
            if (it.type == "book") {
                payload.extra_items.emplace_back(NSID("minecraft:enchanted_book"), ench_set, it.prior_penalty);
            } else {
                auto eq_it = eq_reg.find(NSID(it.id));
                if (eq_it == eq_reg.end())
                    throw std::runtime_error(tr_fmt("cli.err.unknown_equipment", it.id));
                int32_t dur = it.durability > 0 ? it.durability : eq_it->max_durability;
                if (dur > eq_it->max_durability)
                    throw std::runtime_error("durability exceeds max for '" + it.id + "'");
                payload.extra_items.emplace_back(eq_it->id, ench_set, it.prior_penalty, dur);
            }
            payload.extra_item_priorities.push_back(it.priority);
        }
        req.payload = std::move(payload);
    } else {
        req.payload = DirectPayload{build_source(dto.source, ench_reg)};
    }

    // Mode-appropriate default (mirrors the C ABI): dp_merge is direct-only;
    // hamming supports both modes. An empty algorithm in inventory mode MUST
    // NOT resolve to dp_merge (it would throw unsupported_mode).
    req.algorithm = dto.algorithm.empty() ? (inventory ? "hamming" : "dp_merge")
                                          : dto.algorithm;
    req.forge_config.platform = MCE::Java;
    if (dto.max_solutions > 0) req.search_config.max_solutions = dto.max_solutions;
    if (dto.max_search_time_ms > 0) req.search_config.max_search_time = std::chrono::milliseconds(dto.max_search_time_ms);
    if (dto.max_threads > 0) req.search_config.max_threads = dto.max_threads;
    return req;
}

/// 把 JSON payload 包成一条 SSE 帧（event + data，双换行结束）。
std::string sse_frame(const std::string& type, const Json& payload) {
    return "event: " + type + "\ndata: " + payload.to_string() + "\n\n";
}

} // namespace

WebSolveService::WebSolveService(BesqContext& ctx, std::mutex& ctx_gate, web::SseHub* hub)
    : _ctx(ctx), _ctx_gate(ctx_gate), _hub(hub) {}

WebSolveService::~WebSolveService() {
    // Deterministic shutdown: every worker touches *this and _ctx only until
    // its thread finishes, so abort the in-flight executor (making a blocked
    // solve() return promptly) and then join every worker. Mark still-Running
    // tasks Cancelled first so workers that have not yet entered solve() bail
    // out instead of starting a stray solve on a dying context.
    std::vector<std::shared_ptr<Task>> tasks;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        tasks.reserve(_tasks.size());
        for (const auto& [id, t] : _tasks) {
            std::lock_guard<std::mutex> tl(t->mutex);
            if (t->state == TaskState::Running) t->state = TaskState::Cancelled;
            tasks.push_back(t);
        }
    }
    _ctx.abort_solve();
    for (const auto& t : tasks)
        if (t->worker.joinable()) t->worker.join();
}

bool WebSolveService::has_active() const {
    std::lock_guard<std::mutex> lock(_tasks_mutex);
    for (const auto& [id, t] : _tasks) {
        std::lock_guard<std::mutex> tl(t->mutex);
        if (t->state == TaskState::Running) return true;
    }
    return false;
}

std::string WebSolveService::start(const WebTaskDto& dto) {
    std::shared_ptr<Task> task;
    std::string id;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        // Inline the active check — has_active() would re-lock the same mutex.
        // Each task's mutex guards its state against the worker's writes.
        for (const auto& [tid, t] : _tasks) {
            std::lock_guard<std::mutex> tl(t->mutex);
            if (t->state == TaskState::Running)
                throw WebHttpError(409, "TASK_ACTIVE", "a solve is already running");
        }
        // Reap terminal tasks so the table stays bounded (long-lived server;
        // a finished task's result is no longer needed once a new solve starts).
        // A task is erased ONLY after its worker has fully exited (`finished`)
        // and been joined — a terminal state alone is insufficient, because the
        // worker may still be unwinding (e.g. format() after setting Completed,
        // or a cancelled solve() returning). Erasing earlier would let the
        // worker's last shared_ptr release destroy its own still-joinable
        // std::thread → std::terminate. Running tasks are kept so the 409
        // active-check above stays authoritative.
        for (auto it = _tasks.begin(); it != _tasks.end();) {
            auto& t = it->second;
            {
                std::lock_guard<std::mutex> tl(t->mutex);
                if (t->state == TaskState::Running) { ++it; continue; }
            }
            if (!t->finished.load(std::memory_order_acquire)) { ++it; continue; }
            if (t->worker.joinable()) t->worker.join();
            it = _tasks.erase(it);
        }
        task = std::make_shared<Task>();
        task->numeric_id = ++_next_id;
        task->id = "task-" + std::to_string(task->numeric_id);
        id = task->id;
        _tasks[id] = task;
    }

    // Worker thread runs the blocking solve; it outlives this call. The thread
    // is joined by the destructor (not detached) so no worker outlives *this.
    task->worker = std::thread([this, task, dto]() mutable {
        // DoneGuard sets `finished` on every exit path (early cancel returns
        // included), so the reap loop can safely join + erase this task once
        // the worker has stopped touching *this/_ctx.
        struct DoneGuard {
            std::shared_ptr<Task> t;
            ~DoneGuard() { t->finished.store(true, std::memory_order_release); }
        } done_guard{task};
        std::string result_json;
        try {
            {
                // The shared context gate serializes ALL access to the shared
                // BesqContext — against profile mutations on the server thread
                // (WebModule holds the same gate around every ApiProfiles call)
                // and against other workers. Its registries are reached through
                // resolve_effective(), which rebuilds a mutable cache
                // (ProfileManager::_effective_cache/_dep_graph), so overlapping
                // access would be a data race — not even the read-only-looking
                // request build is safe (ctx.enchantments()/equipment() go
                // through it too).
                std::lock_guard<std::mutex> gate_lock(_ctx_gate);
                // Re-check cancellation under the gate: cancel() may have fired
                // while a previous task still held the gate. A cancelled task
                // must never start a stray solve, violating the
                // single-active-slot invariant.
                {
                    std::lock_guard<std::mutex> lock(task->mutex);
                    if (task->state == TaskState::Cancelled) {
                        if (_hub) _hub->unsubscribe_all(task->id);
                        return;
                    }
                }
                auto request = build_request(dto, _ctx);
                {
                    std::lock_guard<std::mutex> lock(task->mutex);
                    if (task->state == TaskState::Cancelled) {
                        if (_hub) _hub->unsubscribe_all(task->id);
                        return;
                    }
                }
                double initial_progress = 0.0;
                if (_hub) {
                    // 初始 progress 帧（I-2b）：solve 前的原子快照。
                    auto prog = _ctx.solve_progress();
                    initial_progress = prog.progress;
                    Json obj = Json::object();
                    obj["type"] = Json("progress");
                    obj["progress"] = Json(prog.progress);
                    _hub->publish(task->id, sse_frame("progress", obj));
                }
                // 周期性 progress 采样（I-2a）：solve() 阻塞本 worker 期间，短命
                // 采样线程每 ~200ms 读一次 _ctx.solve_progress()（线程安全原子读，
                // 专为轮询设计），进度变化即向 hub 发布
                // {"type":"progress","progress":<p>}。采样线程在 solve() 返回的
                // 每个退出路径（正常/异常/cancel）上被停止并 join；终态帧
                // （completed/failed）只会在采样停止后发布。
                struct SamplerStop {
                    std::atomic<bool>* flag;
                    std::thread* th;
                    void stop() {
                        if (!flag) return;
                        flag->store(false, std::memory_order_release);
                        if (th->joinable()) th->join();  // 采样线程至多晚一帧（200ms 内）
                        flag = nullptr;                  // 幂等：已停
                    }
                    ~SamplerStop() { stop(); }
                };
                std::atomic<bool> sampling{true};
                std::thread sampler;
                SamplerStop sampler_stop{_hub ? &sampling : nullptr, &sampler};
                if (_hub) {
                    sampler = std::thread([this, task, &sampling, initial_progress] {
                        double last = initial_progress;  // 与初始帧同基准，进度变了才发
                        for (;;) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            if (!sampling.load(std::memory_order_acquire)) return;
                            const auto prog = _ctx.solve_progress();
                            if (prog.progress == last) continue;
                            last = prog.progress;
                            try {
                                Json obj = Json::object();
                                obj["type"] = Json("progress");
                                obj["progress"] = Json(prog.progress);
                                _hub->publish(task->id, sse_frame("progress", obj));
                            } catch (...) {
                                // 采样帧丢失可接受：hub/帧构造不抛，防御兜底。
                            }
                        }
                    });
                }
                // Single active slot is enforced above; solve() runs to
                // completion (or cancel()). The result carries the executor's
                // real output. solve() 一返回就停采样（join），之后才 format 与
                // 发布终态帧——进度帧严格先于 completed/failed。
                auto result = _ctx.solve(request);
                sampler_stop.stop();
                result_json = _ctx.format(result, request.mode, "json");
            }  // _ctx_gate released before the task-state write
            // Task bookkeeping is NOT gated — commit the result under
            // task->mutex only. A cancel that fired during format() is honored
            // here; the worker must never report a completed task it was asked
            // to cancel.
            {
                std::lock_guard<std::mutex> lock(task->mutex);
                if (task->state == TaskState::Cancelled) {
                    if (_hub) _hub->unsubscribe_all(task->id);
                    return;
                }
                task->state = TaskState::Completed;
                task->result = result_json;  // copy (not move): result_json is still needed for the SSE frame
            }
            if (_hub) {
                Json obj = Json::object();
                obj["type"] = Json("completed");
                try {
                    obj["result"] = Json::parse(result_json);
                } catch (const JsonException&) {
                    obj["result"] = Json(result_json);  // keep a valid envelope even if result isn't strict JSON
                }
                _hub->publish(task->id, sse_frame("completed", obj));
                _hub->unsubscribe_all(task->id);
            }
        } catch (const std::exception& e) {
            std::string error_msg;
            {
                std::lock_guard<std::mutex> lock(task->mutex);
                task->state = TaskState::Failed;
                task->error = e.what();
                error_msg = task->error;
            }
            if (_hub) {
                Json obj = Json::object();
                obj["type"] = Json("failed");
                obj["error"] = Json(error_msg);
                _hub->publish(task->id, sse_frame("failed", obj));
                _hub->unsubscribe_all(task->id);
            }
        }
    });
    return id;
}

TaskStatus WebSolveService::status(const std::string& id) {
    std::shared_ptr<Task> task;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        auto it = _tasks.find(id);
        if (it == _tasks.end())
            throw WebHttpError(404, "TASK_NOT_FOUND", "unknown task: " + id);
        task = it->second;
    }
    TaskStatus out;
    out.task_id = task->numeric_id;
    std::lock_guard<std::mutex> lock(task->mutex);
    out.state = task->state;
    out.error = task->error;
    if (task->state == TaskState::Completed) {
        out.progress = 1.0;
        out.result = task->result;
    } else if (task->state == TaskState::Running) {
        auto prog = _ctx.solve_progress();
        out.progress = prog.progress;
    }
    return out;
}

bool WebSolveService::cancel(const std::string& id) {
    std::shared_ptr<Task> task;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        auto it = _tasks.find(id);
        if (it == _tasks.end()) return false;
        task = it->second;
    }
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        if (task->state != TaskState::Running) return false;
        task->state = TaskState::Cancelled;
    }
    _ctx.abort_solve();  // cancel() on the live executor is a safe no-op if idle
    return true;
}

} // namespace web
