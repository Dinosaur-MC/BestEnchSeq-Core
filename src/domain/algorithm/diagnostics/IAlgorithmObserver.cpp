#include "IAlgorithmObserver.h"
#include "DiagnosticsService.h"

namespace algorithm {

namespace detail {
void attach_observer_to_service(const std::shared_ptr<IAlgorithmObserver> &obs) {
    DiagnosticsService::instance().attach_observer(obs);
}
} // namespace detail

IAlgorithmObserver::~IAlgorithmObserver() {
    // Auto-detach from DiagnosticsService.
    // shared_from_this() is valid here 鈥?the shared_ptr still owns the
    // object at destructor entry, so the enable_shared_from_this weak_ptr
    // is still alive.
    //
    // WARNING: do NOT create global/static IAlgorithmObservers.  They may
    // outlive the DiagnosticsService singleton and trigger UB on detach.
    if (_attached) {
        DiagnosticsService::instance().detach_observer(shared_from_this());
    }
}

} // namespace algorithm
