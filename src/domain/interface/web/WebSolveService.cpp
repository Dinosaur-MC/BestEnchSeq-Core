#include "WebSolveService.h"
#include "common/i18n/Language.h"
#include "common/io/json.h"
#include "domain/algorithm/diagnostics/DiagnosticsService.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/Router.h"
#include "domain/orchestration/types/SolveRequest.h"
#include "domain/orchestration/types/SolveSnapshot.h"
#include "SseHub.h"
#include "WebDiagObserver.h"
#include <chrono>
#include <utility>
#include <vector>

namespace web {

namespace {

/// 附魔 DTO → EnchSet。只保留 level<1 下界检查（A1 审查遗留：快照只校验上界
/// level > max_level，level=0 会静默穿过 apply）；未知魔咒/上界由
/// solve_snapshot 校验（同款错误信息）。
EnchSet build_source(const std::vector<InvEnchDto>& source) {
    EnchSet out;
    for (const auto& e : source) {
        if (e.level < 1)
            throw std::runtime_error(tr_fmt("main.err.ench_level_exceeds_max", e.id, e.level, 1));
        out.emplace(NSID(e.id), std::string{}, e.level);
    }
    return out;
}

/// 目标 DTO → Item。装备定义（未知 id / max_durability）由 solve_snapshot
/// 校验并回填——此处不再查注册表；durability 由 worker 在快照构建后以快照
/// eq（注册表为准）补齐。
Item build_target(const InvTargetDto& t) {
    Item item;
    if (t.item == "book" || t.item == "enchanted_book") {
        item.id = NSID("minecraft:enchanted_book");
    } else {
        item.id = NSID(t.item);
    }
    item.enchantments = build_source(t.enchants);
    return item;
}

SolveRequest build_request(const WebTaskDto& dto, const BesqContext& ctx) {
    // 仅 inventory 分支保留的 durability>max 边界检查需要 eq_reg（A1 审查遗留：
    // 快照不复核耐久）；未知装备走快照校验。
    const auto& eq_reg = ctx.equipment();

    SolveRequest req;
    req.target_item = build_target(dto.target);

    bool inventory = !dto.items.empty();
    req.mode = inventory ? AlgorithmMode::inventory : AlgorithmMode::direct;

    if (inventory) {
        // items → InventoryPayload (mirrors CAbiBindings::parse_inventory_payload)
        InventoryPayload payload;
        for (const auto& it : dto.items) {
            EnchSet ench_set = build_source(it.enchants);
            if (it.type == "book") {
                payload.extra_items.emplace_back(NSID("minecraft:enchanted_book"), ench_set, it.prior_penalty);
            } else {
                auto eq_it = eq_reg.find(NSID(it.id));
                if (eq_it != eq_reg.end()) {
                    int32_t dur = it.durability > 0 ? it.durability : eq_it->max_durability;
                    if (dur > eq_it->max_durability)
                        throw std::runtime_error("durability exceeds max for '" + it.id + "'");
                    payload.extra_items.emplace_back(eq_it->id, ench_set, it.prior_penalty, dur);
                } else {
                    // 未知装备：留原值，solve_snapshot 随即抛 cli.err.unknown_equipment。
                    payload.extra_items.emplace_back(NSID(it.id), ench_set, it.prior_penalty,
                                                     it.durability > 0 ? it.durability : 0);
                }
            }
            payload.extra_item_priorities.push_back(it.priority);
        }
        req.payload = std::move(payload);
    } else {
        req.payload = DirectPayload{build_source(dto.source)};
    }

    // Mode-appropriate default (mirrors the C ABI): dp_merge is direct-only;
    // hamming supports both modes. An empty algorithm in inventory mode MUST
    // NOT resolve to dp_merge (it would throw unsupported_mode).
    req.algorithm = dto.algorithm.empty() ? (inventory ? "hamming" : "dp_merge") : dto.algorithm;
    req.forge_config.platform = MCE::Java;
    // "允许不兼容" 接通（batch C）：wire 键 ignore_incompatible → ForgeConfig 的
    // ignore_imcompatible（内部拼写保留）。默认 false = 严格冲突。
    req.forge_config.ignore_imcompatible = dto.ignore_incompatible;
    if (dto.max_solutions > 0)
        req.search_config.max_solutions = dto.max_solutions;
    if (dto.max_search_time_ms > 0)
        req.search_config.max_search_time = std::chrono::milliseconds(dto.max_search_time_ms);
    if (dto.max_threads > 0)
        req.search_config.max_threads = dto.max_threads;
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
            // Paused 同样算活动槽（batch C）：executor 仍活着，需先取消才能退出。
            // CAS 循环 Running/Paused→Cancelled：竞态失败（pause 翻为 Paused
            // 等）重试；worker 已提交终态（Completed/Failed）则 CAS 失败保留
            // 终态（旧锁语义：终态不被覆盖）。
            TaskState expected = t->state.load(std::memory_order_acquire);
            for (;;) {
                if (expected != TaskState::Running && expected != TaskState::Paused)
                    break;
                if (t->state.compare_exchange_weak(expected, TaskState::Cancelled, std::memory_order_acq_rel,
                                                   std::memory_order_acquire))
                    break;
            }
            tasks.push_back(t);
        }
    }
    _ctx.abort_solve();
    // Publish-window race: a worker may be inside BesqContext::solve() before
    // the pipeline has published the executor handle (resolve_effective /
    // create_executor / simulate — tens of ms on slow filesystems), so the
    // abort above can find no executor and be lost — the solve would then run
    // to completion and this join would block for its full natural duration
    // (a long dp_merge can exceed the test framework's per-case timeout and
    // be pthread_cancelled inside this noexcept destructor → std::terminate).
    // Close the window deterministically: keep aborting until every worker has
    // fully exited (`finished`, set as the worker's very last action — see
    // start()) or a bounded deadline passes.  The moment the pipeline
    // publishes, the next abort's cancel() lands on the live executor (or on
    // an Idle one, recorded as pending) and the run stops at its first
    // cancellation check within milliseconds, so the join below returns
    // promptly.  Each abort on an empty handle is a cheap no-op; the deadline
    // only bounds the poll itself for the (unobserved) case of a worker that
    // never publishes.
    const auto poll_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < poll_deadline) {
        bool any_live = false;
        for (const auto& t : tasks)
            if (!t->finished.load(std::memory_order_acquire)) {
                any_live = true;
                break;
            }
        if (!any_live)
            break;
        _ctx.abort_solve();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (const auto& t : tasks)
        if (t->worker.joinable())
            t->worker.join();
}

bool WebSolveService::has_active() const {
    std::lock_guard<std::mutex> lock(_tasks_mutex);
    for (const auto& [id, t] : _tasks) {
        // Paused 仍占单活动槽（batch C）：executor 活着、求解未结束。
        auto st = t->state.load(std::memory_order_acquire);
        if (st == TaskState::Running || st == TaskState::Paused)
            return true;
    }
    return false;
}

std::string WebSolveService::start(const WebTaskDto& dto) {
    std::shared_ptr<Task> task;
    std::string id;
    // reap 收集区：锁内填充待回收任务，锁外 join+释放（见下方 reap 循环与
    // 锁区后的 join 循环）——任务出表后其引用由 to_reap 持有，生命周期明确。
    std::vector<std::shared_ptr<Task>> to_reap;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        // Inline the active check — has_active() would re-lock the same mutex.
        // Task state is atomic: a snapshot load per task suffices (a single
        // atomic state store is never observably torn).
        // Paused counts as active too (batch C): the executor is still live.
        for (const auto& [tid, t] : _tasks) {
            auto st = t->state.load(std::memory_order_acquire);
            if (st == TaskState::Running || st == TaskState::Paused)
                throw WebHttpError(409, "TASK_ACTIVE", "a solve is already running");
        }
        // Reap terminal tasks so the table stays bounded (long-lived server;
        // a finished task's result is no longer needed once a new solve starts).
        // 锁内只收集待回收任务；join/释放全部在锁外完成——持锁跨 join 是坏
        // 味道：取消路径下 worker 收尾可能拉长 loop 线程（submit 调用方）的
        // 持锁时间。任务状态在 worker 退出后不再变化，锁外二次确认恒真。
        // A task is collected ONLY after its worker has fully exited
        // (`finished`) — a terminal state alone is insufficient, because the
        // worker may still be unwinding (e.g. format() after setting Completed,
        // or a cancelled solve() returning). Erasing earlier would let the
        // worker's last shared_ptr release destroy its own still-joinable
        // std::thread → std::terminate. Running tasks are kept so the 409
        // active-check above stays authoritative.
        for (auto it = _tasks.begin(); it != _tasks.end();) {
            auto& t = it->second;
            // Paused 同 Running 保留（batch C）：非终态，不能回收。
            auto st = t->state.load(std::memory_order_acquire);
            if (st == TaskState::Running || st == TaskState::Paused) {
                ++it;
                continue;
            }
            if (!t->finished.load(std::memory_order_acquire)) {
                ++it;
                continue;
            }
            to_reap.push_back(t);
            it = _tasks.erase(it); // 先出表（锁内），引用由 to_reap 持有
        }
        task = std::make_shared<Task>();
        task->numeric_id = ++_next_id;
        task->id = "task-" + std::to_string(task->numeric_id);
        id = task->id;
        _tasks[id] = task;
    }

    // join 锁外执行：finished==true 已保证 worker 完全退出，join 即时返回；
    // to_reap 随本函数结束在锁外析构（Task 已 join，shared_ptr 释放安全）。
    for (auto& t : to_reap)
        if (t->worker.joinable())
            t->worker.join();

    // Worker thread runs the blocking solve; it outlives this call. The thread
    // is joined by the destructor (not detached) so no worker outlives *this.
    task->worker = std::thread([this, task, dto]() mutable {
        // DoneGuard sets `finished` on every exit path (early cancel returns
        // included), so the start() reap pass can safely collect (锁内) and
        // join + erase this task once the worker has stopped touching
        // *this/_ctx.
        struct DoneGuard {
            std::shared_ptr<Task> t;
            ~DoneGuard() { t->finished.store(true, std::memory_order_release); }
        } done_guard{task};
        std::string result_json;
        try {
            // ── 快照 + 请求（gate 内，µs-ms）──
            // The shared context gate serializes access to the shared
            // BesqContext's ProfileManager effective view: resolve_effective()
            // rebuilds a mutable cache (_effective_cache/_dep_graph), so
            // overlapping access against server-thread profile mutations would
            // be a data race. Only the snapshot build (and format below) go
            // through it — everything between runs on the self-contained
            // snapshot: solve() is lock-free, zero profile references.
            SolveRequest request;
            orchestration::SolveSnapshot snapshot;
            {
                std::lock_guard<std::mutex> gate_lock(_ctx_gate);
                // Re-check cancellation under the gate: cancel() may have fired
                // while a previous task still held the gate. A cancelled task
                // must never start a stray solve, violating the
                // single-active-slot invariant.
                if (task->state.load(std::memory_order_acquire) == TaskState::Cancelled) {
                    if (_hub)
                        _hub->unsubscribe_all(task->id);
                    return;
                }
                request = build_request(dto, _ctx);      // 纯 DTO→请求（下界/边界校验）
                snapshot = _ctx.solve_snapshot(request); // 校验+剪枝（未知/超限在此抛）
                // 回填目标装备 max_durability：build_request 不再查注册表，而
                // CompactAdapter::apply 透传 request.durability——以快照 eq
                // （注册表为准）补齐，保持旧 build_target 语义。
                if (!request.target_item.is_book()) {
                    auto eq_it = snapshot.eq().find(request.target_item.id);
                    if (eq_it != snapshot.eq().end())
                        request.target_item.durability = eq_it->max_durability;
                }
            } // gate 释放——solve 全程无锁（快照自包含，零 profile 引用）
            // 取消复查（gate 外；状态是原子字）：gate 段很短，但取消仍可能
            // 落在构建期间——solve 前的最后一道闸。
            if (task->state.load(std::memory_order_acquire) == TaskState::Cancelled) {
                if (_hub)
                    _hub->unsubscribe_all(task->id);
                return;
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
                    if (!flag)
                        return;
                    flag->store(false, std::memory_order_release);
                    if (th->joinable())
                        th->join(); // 采样线程至多晚一帧（200ms 内）
                    flag = nullptr; // 幂等：已停
                }
                ~SamplerStop() { stop(); }
            };
            std::atomic<bool> sampling{true};
            std::thread sampler;
            SamplerStop sampler_stop{_hub ? &sampling : nullptr, &sampler};
            if (_hub) {
                sampler = std::thread([this, task, &sampling, initial_progress] {
                    double last = initial_progress; // 与初始帧同基准，进度变了才发
                    for (;;) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        if (!sampling.load(std::memory_order_acquire))
                            return;
                        const auto prog = _ctx.solve_progress();
                        if (prog.progress == last)
                            continue;
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
            // ── 算法诊断事件流（T2）──
            // 观察者必须在 solve() 前注册：executor 在 solve() 内部创建并
            // start()，事件从算法执行一开始（首个 Progress）就推送。
            // attach 前先 flush() 排空上一任务可能仍停留在诊断队列中的
            // 尾部事件——EventLoop 是异步派发，单活动槽保证当前任务的事件
            // 是 attach 期间唯一的事件源，但上一任务的 Exit 等事件可能在
            // 本任务 attach 后才被派发，不排空会串任务误归属。
            //
            // 沙箱模式限制：BESQ_SANDBOX=1 时 executor 在 besq-worker
            // 子进程中运行，诊断事件推送发生在子进程的 DiagnosticsService
            // 里（沙箱无诊断 IPC 桥回父进程），因此沙箱任务不产生任何
            // 诊断事件（diagnostics 为空、diag_exit 为 null）。
            algorithm::DiagnosticsService::instance().flush();
            auto diag_observer = algorithm::IAlgorithmObserver::create<WebDiagObserver>([this, task](Json event) {
                // 先序列化 SSE 帧（随后 event 被 move 进 diagnostics）。
                std::string frame;
                if (_hub)
                    frame = sse_frame("diag", event);
                // 仅 diagnostics 快照（其余字段已原子化，此锁不再护状态）。
                {
                    std::lock_guard<std::mutex> lock(task->mutex);
                    if (task->diagnostics.size() >= 500)
                        task->diagnostics.erase(task->diagnostics.begin());
                    // exit 事件额外存一份结构化 KV（副本：列表各持一份）。
                    if (event.has("kind") && event["kind"].as<std::string>() == "exit")
                        task->diag_exit = event;
                    task->diagnostics.push_back(std::move(event));
                }
                if (_hub)
                    _hub->publish(task->id, std::move(frame));
            });
            // Single active slot is enforced above; solve() runs to
            // completion (or cancel()). The result carries the executor's
            // real output. solve() 一返回就停采样（join），之后才 format 与
            // 发布终态帧——进度帧严格先于 completed/failed。
            // solve 跑在自包含快照上（P0 锁攻破）：无 gate、无 profile 引用。
            auto result = _ctx.solve(request, snapshot);
            sampler_stop.stop();
            // solve() 返回前 exit 事件必已入队（_finalize 在 wait() 内同步
            // 执行），但 EventLoop 是异步派发——必须 flush() 到派发完成
            // （含本观察者收到 exit）后再 detach，否则尾部事件会因 detach
            // 而丢失。flush() 返回时所有已入队事件的 observer 回调均已执行
            // 完毕，随后 detach 不存在在途回调。
            algorithm::DiagnosticsService::instance().flush();
            // 显式 detach，而不是依赖析构自动 detach：DiagnosticsService
            // 的 _observers 持有本观察者的第二个 shared_ptr，局部变量析构
            // 只把引用计数 2→1，析构函数（及其中断的自动 detach）根本不会
            // 运行——不显式 detach 会让观察者永久滞留（长跑服务器累积泄漏，
            // 且进程退出时单例析构会在 use_count==0 时触发 shared_from_this
            // 的 bad_weak_ptr → abort）。
            algorithm::DiagnosticsService::instance().detach_observer(diag_observer);
            // format() 走 resolve_effective（ProfileManager 有效视图缓存
            // 重建，无内部锁）——短暂重新取 gate（ms 级；旧路径是 solve
            // 全程持锁，秒~分钟级），profile 读写只在此窗口排队。
            {
                std::lock_guard<std::mutex> gate_lock(_ctx_gate);
                result_json = _ctx.format(result, request.mode, "json");
            }
            // Task bookkeeping is NOT gated — commit atomically. A cancel that
            // fired during format() is honored here; the worker must never
            // report a completed task it was asked to cancel. Order: result
            // first (release), then CAS state → Completed — once Completed is
            // observable to a status() acquire read, the result is guaranteed
            // visible. A pause that lost the race is a lost no-op and the task
            // simply completes (documented); cancel wins (bail above).
            task->result.store(std::make_shared<const std::string>(result_json),
                               std::memory_order_release); // copy (not move): result_json is still needed for the SSE frame
            {
                TaskState expected = task->state.load(std::memory_order_acquire);
                for (;;) {
                    if (expected == TaskState::Cancelled) {
                        if (_hub)
                            _hub->unsubscribe_all(task->id);
                        return;
                    }
                    if (task->state.compare_exchange_weak(expected, TaskState::Completed, std::memory_order_release,
                                                          std::memory_order_acquire))
                        break;
                }
            }
            if (_hub) {
                Json obj = Json::object();
                obj["type"] = Json("completed");
                try {
                    obj["result"] = Json::parse(result_json);
                } catch (const JsonException&) {
                    obj["result"] = Json(result_json); // keep a valid envelope even if result isn't strict JSON
                }
                _hub->publish(task->id, sse_frame("completed", obj));
                _hub->unsubscribe_all(task->id);
            }
        } catch (const std::exception& e) {
            std::string error_msg = e.what();
            // error 先（release）、state 后（release）：Failed 一旦可观察，
            // error 必已可见。无条件提交（旧锁语义）：异常路径下任务以
            // Failed 告终（即使 cancel 已置 Cancelled，取消意图仍经
            // abort_solve 实现，状态字以 worker 终态为准）。
            task->error.store(std::make_shared<const std::string>(error_msg), std::memory_order_release);
            task->state.store(TaskState::Failed, std::memory_order_release);
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
    out.state = task->state.load(std::memory_order_acquire);
    if (auto p = task->error.load(std::memory_order_acquire))
        out.error = *p;
    // 诊断字段随快照一起拷贝（上限 500，拷贝代价有界）；仅 worker 写、
    // 这里拷贝 → task->mutex 只护这两者。
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        out.diagnostics = task->diagnostics;
        out.diag_exit = task->diag_exit;
    }
    if (out.state == TaskState::Completed) {
        out.progress = 1.0;
        if (auto p = task->result.load(std::memory_order_acquire))
            out.result = *p;
    } else if (out.state == TaskState::Running || out.state == TaskState::Paused) {
        // Paused 时 executor 冻结在暂停点，progress() 返回冻结值（batch C）。
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
        if (it == _tasks.end())
            return false;
        task = it->second;
    }
    // CAS Running/Paused→Cancelled：终态（Completed/Failed）不被覆盖——与旧
    // 锁语义一致（非 Running/Paused 直接返回 false）。
    {
        TaskState expected = task->state.load(std::memory_order_acquire);
        for (;;) {
            // Paused 也可取消（batch C）：abort_solve 的 exec->cancel() 会唤醒
            // 冻结在暂停点的算法线程。
            if (expected != TaskState::Running && expected != TaskState::Paused)
                return false;
            if (task->state.compare_exchange_weak(expected, TaskState::Cancelled, std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
                break;
        }
    }
    _ctx.abort_solve(); // cancel() on the live executor is a safe no-op if idle
    // Publish-window retry (same race as the destructor): if the worker is
    // inside BesqContext::solve() before the pipeline published the executor
    // handle, the first abort found nothing.  The retry lands on an Idle
    // executor and is recorded as pending, aborting the run at its first
    // cancellation check instead of burning the full solve.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    _ctx.abort_solve();
    return true;
}

bool WebSolveService::pause(const std::string& id) {
    std::shared_ptr<Task> task;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        auto it = _tasks.find(id);
        if (it == _tasks.end())
            throw WebHttpError(404, "TASK_NOT_FOUND", "unknown task: " + id);
        task = it->second;
    }
    // CAS Running→Paused：失败时 expected 携带实际状态，错误信息与旧锁版一致。
    {
        TaskState expected = TaskState::Running;
        if (!task->state.compare_exchange_strong(expected, TaskState::Paused, std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            throw WebHttpError(409, "TASK_NOT_PAUSABLE",
                               expected == TaskState::Paused ? "task is already paused" : "task is not running");
    }
    // 触发 executor 暂停。与 cancel 相同的发布窗口竞态：executor 句柄未发布时
    // pause_solve 是空操作，任务直接跑完（web 层状态随后被 worker 提交为
    // Completed）——单槽语义在两条路径下都成立。20ms 后重试一次：此刻管线已
    // 发布 executor，第二次 pause 落在 Running 的 executor 上并真正冻结（对
    // 已冻结/已完成的 executor 是安全空操作），避免慢求解任务上暂停永久落空。
    _ctx.pause_solve();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    _ctx.pause_solve();
    return true;
}

bool WebSolveService::resume(const std::string& id) {
    std::shared_ptr<Task> task;
    {
        std::lock_guard<std::mutex> lock(_tasks_mutex);
        auto it = _tasks.find(id);
        if (it == _tasks.end())
            throw WebHttpError(404, "TASK_NOT_FOUND", "unknown task: " + id);
        task = it->second;
    }
    // CAS Paused→Running：仅暂停态可恢复（与旧锁版一致）。
    {
        TaskState expected = TaskState::Paused;
        if (!task->state.compare_exchange_strong(expected, TaskState::Running, std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            throw WebHttpError(409, "TASK_NOT_RESUMABLE", "task is not paused");
    }
    _ctx.resume_solve();
    return true;
}

} // namespace web
