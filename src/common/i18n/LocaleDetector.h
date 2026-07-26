#pragma once
#include <string>

/// Detect the system's preferred POSIX locale string.
/// Returns e.g. "zh_CN", "en_US", "de_DE", etc.
/// Falls back to "en_US" on any failure.
std::string detect_system_locale();
