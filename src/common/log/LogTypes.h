#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warn,
    Error
};

struct LogEntry {
    LogLevel level;
    std::string message;
    /// True when the console mirror already printed this entry synchronously
    /// (the LOG_* sync path) — the worker's ConsoleConsumer must skip it so
    /// it is not printed twice; the FileConsumer still writes it.
    bool console_printed = false;
};

/// Typed Logger configuration.  AppConfig loads the BESQ_LOG_* env vars and
/// produces one of these; setup_logger() applies it to the singleton.
/// level / console_level map: 0=Debug, 1=Info, 2=Warn, 3=Error.
struct LoggerConfig {
    int32_t level           = 0;    // file-log threshold
    size_t  retention       = 5;    // max historic log files kept during rotation
    bool    console_enabled = true; // mirror to stderr (Warn/Error) / stdout (Debug/Info)
    int32_t console_level   = 2;    // console mirror threshold
    std::string log_dir     = "logs";
};
