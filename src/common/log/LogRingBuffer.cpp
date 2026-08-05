#include "common/log/LogRingBuffer.h"
#include <algorithm>   // std::min (snapshot tail)
#include <chrono>

LogRingBuffer::LogRingBuffer(size_t capacity) : _capacity(capacity) {}

void LogRingBuffer::push(LogLevel level, std::string message) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    LogRecord record{level, now, std::move(message)};
    _buffer.push_back(record);
    if (_buffer.size() > _capacity)
        _buffer.pop_front();
    // Listeners are invoked UNDER the lock: remove_listener() then hard-
    // synchronizes — after it returns no listener invocation is in flight, so
    // a listener capturing an owning object (e.g. WebModule::Impl) cannot be
    // invoked on a destroyed owner during teardown. Contract: a listener must
    // NOT call back into this ring (add/remove/push would deadlock) and must
    // not block on work that logs synchronously.
    for (const auto& [id, fn] : _listeners)
        fn(record);
}

LogRingBuffer::ListenerId LogRingBuffer::add_listener(Listener fn) {
    std::lock_guard<std::mutex> lock(_mutex);
    ListenerId id = ++_next_listener;
    _listeners.emplace_back(id, std::move(fn));
    return id;
}

void LogRingBuffer::remove_listener(ListenerId id) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _listeners.begin(); it != _listeners.end(); ++it) {
        if (it->first == id) {
            _listeners.erase(it);
            break;
        }
    }
}

std::vector<LogRecord> LogRingBuffer::snapshot(LogLevel min_level, size_t tail) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<LogRecord> out;
    out.reserve(std::min(_buffer.size(), tail));
    for (const auto& e : _buffer)
        if (e.level >= min_level)
            out.push_back(e);
    if (out.size() > tail)
        out.erase(out.begin(), out.begin() + (out.size() - tail));
    return out;
}

void LogRingBuffer::clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _buffer.clear();
}
