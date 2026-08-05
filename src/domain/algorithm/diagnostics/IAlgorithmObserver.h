#pragma once
#include "domain/algorithm/diagnostics/DiagnosticsEvent.h"
#include "domain/algorithm/types/AlgorithmState.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/Solution.h"
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace algorithm {

/// Simple diagnostic message.
struct DiagnosticInfo {
    std::string message;
};

// 鈹€鈹€鈹€ Observer (streaming callbacks, compact-only) 鈹€鈹€鈹€
//
// Create via the factory method:
//   auto obs = IAlgorithmObserver::create();
// or for a subclass:
//   auto obs = IAlgorithmObserver::create<MyObserver>(ctor_args...);
//
// The factory auto-attaches to DiagnosticsService.
// The destructor auto-detaches.  Do NOT create global/static observers 鈥?// they may outlive the DiagnosticsService singleton.

class IAlgorithmObserver;

namespace detail {
/// Implemented in IAlgorithmObserver.cpp to break the circular dependency
/// between IAlgorithmObserver.h and DiagnosticsService.h.
void attach_observer_to_service(const std::shared_ptr<IAlgorithmObserver> &obs);
} // namespace detail

class IAlgorithmObserver : public std::enable_shared_from_this<IAlgorithmObserver> {
  public:
    /// Factory: create + auto-attach to DiagnosticsService.
    /// Defined after class body (below) so IAlgorithmObserver is complete.
    template <typename T = IAlgorithmObserver, typename... Args>
    static std::shared_ptr<T> create(Args &&...args);

    /// Returns true while this observer is still attached to the
    /// DiagnosticsService.
    bool is_attached() const noexcept { return _attached; }

    virtual ~IAlgorithmObserver();

    IAlgorithmObserver(const IAlgorithmObserver &)            = delete;
    IAlgorithmObserver &operator=(const IAlgorithmObserver &) = delete;

    /// Return false to suppress all callbacks for the given task_id.
    /// Default: accept all tasks.
    virtual bool accept_task_id(size_t) const { return true; }

    virtual void on_progress(size_t task_id, uint8_t pct, ProgressStatus status) {}
    virtual void on_solution_found(size_t task_id, const std::vector<EnchStep> &solution) {}
    virtual void on_state_changed(size_t task_id, AlgorithmState prev, AlgorithmState curr) {}
    virtual void on_diagnostic(size_t task_id, const DiagnosticInfo &info) {}
    virtual void on_completed(size_t task_id, const AlgorithmOutput &output) {}
    /// Exit 事件的结构化载荷（算法名 + 退出状态/耗时/计数器 + AlgorithmDiagnostics
    /// KV）。默认 no-op；需要 exit 明细的观察者覆写。仅在 Exit 事件派发时调用，
    /// 与 on_diagnostic/on_completed 同批（同一事件）。
    virtual void on_exit(size_t task_id, std::string_view algorithm_name,
                         const DiagnosticsEvent::ExitPayload &payload) {}

  protected:
    IAlgorithmObserver() = default;

  private:
    friend class DiagnosticsService;
    bool _attached = false;
};

template <typename T, typename... Args>
inline std::shared_ptr<T> IAlgorithmObserver::create(Args &&...args) {
    static_assert(std::is_base_of_v<IAlgorithmObserver, T>,
                  "T must derive from IAlgorithmObserver");
    auto obs = std::make_shared<T>(std::forward<Args>(args)...);
    detail::attach_observer_to_service(obs);
    return obs;
}

} // namespace algorithm
