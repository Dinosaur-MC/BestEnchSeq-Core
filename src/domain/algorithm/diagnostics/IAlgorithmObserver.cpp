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
    //
    // WARNING: do NOT create global/static IAlgorithmObservers.  They may
    // outlive the DiagnosticsService singleton and trigger UB on detach.
    if (_attached) {
        // Last-resort detach for the common case (a local shared_ptr owner
        // goes out of scope).  NOTE: this only fires when the caller's
        // shared_ptr is the LAST owner — DiagnosticsService's own _observers
        // vector holds a second reference, so callers that want the observer
        // removed mid-run MUST detach_observer() explicitly (the destructor
        // will not run while the vector still owns it).
        try {
            DiagnosticsService::instance().detach_observer(shared_from_this());
        } catch (const std::bad_weak_ptr&) {
            // Singleton teardown releasing the last reference: the control
            // block is already gone, so shared_from_this() cannot produce a
            // handle.  The service itself is being destroyed — nothing left
            // to detach from; swallowing avoids an exception escaping this
            // (implicitly noexcept) destructor, which would abort the process.
        }
    }
}

} // namespace algorithm
