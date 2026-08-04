#pragma once
#include "common/log/LogTypes.h"
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
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
class LogRingBuffer {
public:
    explicit LogRingBuffer(size_t capacity = 1024);

    void push(LogLevel level, std::string message);

    /// Return up to `tail` entries at or above `min_level`, oldest first.
    std::vector<LogRecord> snapshot(LogLevel min_level, size_t tail) const;

    void clear();

private:
    mutable std::mutex _mutex;
    std::deque<LogRecord> _buffer;
    size_t _capacity;
};
