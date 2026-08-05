#pragma once
#include "common/log/LogTypes.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/// One retained log line for /api/logs. Named LogRecord to avoid colliding
/// with the existing global `LogEntry` in LogTypes.h (Logger's file/console
/// entry type). Global namespace — this is a common/log component, not a web
/// module type.
struct LogRecord {
    LogLevel level;
    int64_t timestamp_ms;
    std::string message;
};

/// Bounded thread-safe ring buffer of recent log entries.
/// The Logger pushes here via set_ring_buffer(); ApiLogs snapshots it.
///
/// Live listeners (added with add_listener) are notified on every push().
/// Used by the web layer to fan log lines out to `/api/logs/events` SSE
/// subscribers. Listeners are invoked under the ring's mutex — a listener
/// must not call back into the ring, and remove_listener() synchronizes
/// with in-flight invocations (safe teardown for owner-capturing listeners).
class LogRingBuffer {
public:
    using Listener = std::function<void(const LogRecord&)>;
    using ListenerId = uint64_t;

    explicit LogRingBuffer(size_t capacity = 1024);

    void push(LogLevel level, std::string message);

    /// Return up to `tail` entries at or above `min_level`, oldest first.
    std::vector<LogRecord> snapshot(LogLevel min_level, size_t tail) const;

    void clear();

    /// Register a listener notified (out-of-lock) on every push(). Returns a
    /// token for remove_listener(). Backward compatible — snapshot()/clear()
    /// are unchanged and listeners are not invoked by them.
    ListenerId add_listener(Listener fn);

    /// Remove a previously registered listener (no-op for unknown tokens).
    void remove_listener(ListenerId id);

private:
    mutable std::mutex _mutex;
    std::deque<LogRecord> _buffer;
    std::vector<std::pair<ListenerId, Listener>> _listeners;
    ListenerId _next_listener = 0;
    size_t _capacity;
};
