#pragma once

/// @file common/utils/ExeDir.hpp
/// Cross-platform current-executable-directory lookup.
///
/// All runtime default file paths (profiles/, logs/, states/, config.json, …)
/// resolve against exe_dir() so the application behaves identically no matter
/// which working directory it was launched from.  Returns an empty path when
/// the location cannot be determined — callers fall back to the working
/// directory (see the design spec 2026-08-14-exe-dir-defaults-design.md).

#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

/// Directory containing the current executable (best-effort; empty if
/// unknown).  Cached after the first call — the executable path is fixed for
/// the process lifetime.
inline std::filesystem::path exe_dir() noexcept {
    static const std::filesystem::path cached = []() -> std::filesystem::path {
#if defined(_WIN32)
        // GetModuleFileNameW with a growing buffer — MAX_PATH truncation must
        // not silently cut the path.
        DWORD size = MAX_PATH;
        std::wstring buf(size, L'\0');
        for (;;) {
            const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0)
                return {};
            if (n < buf.size()) {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path(buf).parent_path();
#elif defined(__linux__)
        // readlink("/proc/self/exe") with a growing buffer.
        size_t size = 256;
        for (;;) {
            std::string buf(size, '\0');
            const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
            if (n < 0)
                return {};
            if (static_cast<size_t>(n) < buf.size() - 1) {
                buf.resize(static_cast<size_t>(n));
                return std::filesystem::path(buf).parent_path();
            }
            size *= 2;
        }
#elif defined(__APPLE__)
        // _NSGetExecutablePath: first call returns the required buffer size.
        uint32_t size = 0;
        if (::_NSGetExecutablePath(nullptr, &size) != 0) {
            std::string buf(size, '\0');
            if (::_NSGetExecutablePath(buf.data(), &size) != 0)
                return {};
            return std::filesystem::path(buf).parent_path();
        }
        return {};
#else
        return {};
#endif
    }();
    return cached;
}
