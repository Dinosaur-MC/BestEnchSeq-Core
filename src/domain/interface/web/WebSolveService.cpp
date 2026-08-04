#include "WebSolveService.h"
#include "domain/interface/BesqContext.h"
#include "domain/orchestration/types/SolveRequest.h"
#include "common/i18n/Language.h"
#include <chrono>
#include <utility>
#include <vector>

namespace webhttp {

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

} // namespace

WebSolveService::WebSolveService(BesqContext& ctx) : _ctx(ctx) {}

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
                throw WebHttpError(409, "a solve is already running");
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
        try {
            // The gate serializes ALL access to the shared BesqContext. Its
            // registries are reached through resolve_effective(), which rebuilds
            // a mutable cache (ProfileManager::_effective_cache/_dep_graph), so
            // two workers may never overlap — not even on the read-only-looking
            // request build (ctx.enchantments()/equipment() go through it too).
            std::lock_guard<std::mutex> gate(_solve_mutex);
            // Re-check cancellation under the gate: cancel() may have fired
            // while a previous task still held the gate. A cancelled task must
            // never start a stray solve, violating the single-active-slot
            // invariant.
            {
                std::lock_guard<std::mutex> lock(task->mutex);
                if (task->state == TaskState::Cancelled) return;
            }
            auto request = build_request(dto, _ctx);
            {
                std::lock_guard<std::mutex> lock(task->mutex);
                if (task->state == TaskState::Cancelled) return;
            }
            // Single active slot is enforced above; solve() runs to completion
            // (or cancel()). The result carries the executor's real output.
            auto result = _ctx.solve(request);
            std::string json;
            {
                std::lock_guard<std::mutex> lock(task->mutex);
                if (task->state == TaskState::Cancelled) return;
                task->state = TaskState::Completed;
                task->progress = 1.0;
                json = _ctx.format(result, request.mode, "json");
                task->result = std::move(json);
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(task->mutex);
            task->state = TaskState::Failed;
            task->error = e.what();
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
            throw WebHttpError(404, "unknown task: " + id);
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

} // namespace webhttp
