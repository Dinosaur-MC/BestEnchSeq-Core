#pragma once
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

// Exception type for test assertions (distinct from application exceptions)
class test_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Test counters — atomic: per-case 超时机制下卡死线程与主线程可能并发计数
// （超时后被杀线程的残余增量与 print_summary 的读取），原子化消除数据竞争。
// 既有 ++/+= 用法与隐式读完全兼容。
inline std::atomic<int> tests_passed = 0;
inline std::atomic<int> tests_failed = 0;

// Assertion-level verbose flag (--verbose from the shared main).
inline bool test_verbose = false;

namespace detail {

// Convert a value to a human-readable string for error messages
template <typename T> std::string fmt_val(const T& val) {
    if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<int>(val));
    } else if constexpr (std::is_same_v<T, bool>) {
        return val ? "true" : "false";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return val;
    } else if constexpr (std::is_same_v<T, const char*>) {
        return val ? std::string(val) : "(null)";
    } else {
        std::ostringstream oss;
        oss << val;
        return oss.str();
    }
}

} // namespace detail

// Basic boolean assertion.
// Wraps the condition in a try/catch for exception safety.
inline void expect(bool condition, const std::string& message) {
    try {
        if (!condition) {
            tests_failed++;
            throw test_error(message);
        }
        tests_passed++;
        if (test_verbose)
            std::cout << "  OK: " << message << std::endl;
    } catch (const test_error&) {
        throw;
    } catch (...) {
        tests_failed++;
        throw test_error(message + " (unexpected exception in condition)");
    }
}

// Equality assertion. Shows both values on failure.
template <typename T, typename U> void expect_eq(const T& actual, const U& expected, const std::string& message) {
    try {
        if (!(actual == expected)) {
            tests_failed++;
            std::string msg = message + " - expected: " + detail::fmt_val(expected) + ", actual: " + detail::fmt_val(actual);
            throw test_error(msg);
        }
        tests_passed++;
        if (test_verbose)
            std::cout << "  OK: " << message << std::endl;
    } catch (const test_error&) {
        throw;
    } catch (...) {
        tests_failed++;
        throw test_error(message + " (unexpected exception in expect_eq)");
    }
}

// Asserts that an expression throws any exception.
template <typename F> void expect_throws(F&& expr, const std::string& message) {
    try {
        expr();
        tests_failed++;
        throw test_error(message + " - expected exception but none thrown");
    } catch (const test_error&) {
        throw; // propagate test assertion failures
    } catch (...) {
        tests_passed++; // caught any exception — success
        if (test_verbose)
            std::cout << "  OK: " << message << std::endl;
    }
}

// Asserts that an expression throws a specific exception type.
template <typename E, typename F> void expect_throws_as(F&& expr, const std::string& message) {
    try {
        expr();
        tests_failed++;
        throw test_error(message + " - expected exception but none thrown");
    } catch (const test_error&) {
        throw; // Always re-throw test errors first (before E catch)
    } catch (const E&) {
        tests_passed++;
        if (test_verbose)
            std::cout << "  OK: " << message << std::endl;
    } catch (const std::exception& e) {
        tests_failed++;
        std::string msg = message + " - expected " + typeid(E).name() + " but got: " + e.what();
        throw test_error(msg);
    } catch (...) {
        tests_failed++;
        throw test_error(message + " - unexpected exception type");
    }
}

// Floating-point approximate equality comparison.
inline void expect_approx(double actual, double expected, double epsilon, const std::string& message) {
    try {
        if (std::fabs(actual - expected) > epsilon) {
            tests_failed++;
            std::string msg = message + " - expected: " + detail::fmt_val(expected) + " pm " + detail::fmt_val(epsilon) +
                              ", actual: " + detail::fmt_val(actual);
            throw test_error(msg);
        }
        tests_passed++;
        if (test_verbose)
            std::cout << "  OK: " << message << std::endl;
    } catch (const test_error&) {
        throw;
    } catch (...) {
        tests_failed++;
        throw test_error(message + " (unexpected exception in expect_approx)");
    }
}

// Helper macro: increments the test counter WITHOUT printing——框架共享 main
// 已逐 case 打印 "PASS: <name>"，宏内打印会造成双 PASS 行（遗留清理，
// 2026-08-08）。计数语义不变（迁移基线依赖其计数）。
#define TEST_PASS(name)                                                                                                        \
    do {                                                                                                                       \
        ++tests_passed;                                                                                                        \
    } while (false)

// Print test summary and return exit code (0 = all passed, 1 = failures).
inline int print_summary() {
    int total = tests_passed + tests_failed;
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed, " << total << " total" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
