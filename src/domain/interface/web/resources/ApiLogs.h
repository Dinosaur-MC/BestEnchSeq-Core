#pragma once
#include "common/log/LogTypes.h"
#include <cstddef>
#include <string>

class LogRingBuffer;  // common/log/LogRingBuffer.h

/// GET /api/logs?level=...&tail=N — recent log lines from the ring buffer.
struct ApiLogs {
    static std::string handle(const LogRingBuffer& ring,
                              LogLevel min_level, size_t tail);
};
