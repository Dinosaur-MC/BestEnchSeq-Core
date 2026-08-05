#pragma once
#include "common/io/json.h"
#include "domain/algorithm/diagnostics/DiagnosticsEvent.h"
#include "domain/algorithm/diagnostics/IAlgorithmObserver.h"
#include <functional>

namespace web {

/// 算法诊断事件 → 紧凑 JSON 桥（T2）。
///
/// 把 DiagnosticsService 异步派发的事件转成前端可展示的 {kind:...} JSON
/// （progress / state / exit），经 on_event 回调交回。回调运行在诊断
/// EventLoop 线程上——实现必须线程安全（WebSolveService 侧在回调内加
/// task 锁 + 经 SseHub 发布）。
///
/// 用 IAlgorithmObserver::create<WebDiagObserver>(...) 工厂创建即自动
/// attach。注意：DiagnosticsService 的 _observers 持有第二个 shared_ptr，
/// 局部变量析构不会触发析构函数（引用计数只降到 1），因此使用方必须在
/// 事件流结束后显式 detach_observer()（WebSolveService 的 worker 即如此）；
/// 析构函数的自动 detach 只在调用方是最后持有者时才生效。
class WebDiagObserver : public algorithm::IAlgorithmObserver {
public:
    /// 每事件回调：收到转出的紧凑 JSON（{"kind":"progress"|"state"|"exit",...}），
    /// 生命周期移交给回调。回调运行在诊断 EventLoop 线程。
    using OnEvent = std::function<void(Json)>;

    explicit WebDiagObserver(OnEvent on_event) : _on_event(std::move(on_event)) {}

    // ── IAlgorithmObserver ────────────────────────────────────────────
    // Solution / on_diagnostic 不转发：前端不需要方案步明细与文件持久化同款
    // 的纯文本诊断行（exit 已带结构化 KV）。
    void on_progress(size_t task_id, uint8_t pct,
                     algorithm::ProgressStatus status) override;
    void on_state_changed(size_t task_id, algorithm::AlgorithmState prev,
                          algorithm::AlgorithmState curr) override;
    void on_exit(size_t task_id, std::string_view algorithm_name,
                 const algorithm::DiagnosticsEvent::ExitPayload& payload) override;

private:
    OnEvent _on_event;
};

} // namespace web
