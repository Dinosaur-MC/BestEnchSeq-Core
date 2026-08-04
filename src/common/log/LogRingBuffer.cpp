#include "common/log/LogRingBuffer.h"
#include <algorithm>   // std::min (snapshot tail)
#include <chrono>

LogRingBuffer::LogRingBuffer(size_t capacity) : _capacity(capacity) {}

void LogRingBuffer::push(LogLevel level, std::string message) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    _buffer.push_back(LogRecord{level, now, std::move(message)});
    if (_buffer.size() > _capacity)
        _buffer.pop_front();
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
