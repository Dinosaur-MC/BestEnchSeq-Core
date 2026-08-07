// Test-framework self-test: registration / arg parsing / exception fallback /
// SKIP. The ctest registration only runs --filter "positive:" (exit 0);
// negative paths are verified manually per the plan.
// 注意：#define 必须在唯一的 include 之前（pragma once 会让第二次 include 空转）。
#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include <stdexcept>

TEST_CASE("positive: registry runs") {
    expect(true, "trivial assertion");
}

TEST_CASE("positive: expect_eq") {
    expect_eq(1 + 1, 2, "addition");
}

TEST_CASE("negative: assert failure") {
    expect(false, "intentional failure for self-test");
}

TEST_CASE("negative: std exception") {
    throw std::runtime_error("boom");
}

TEST_CASE("negative: unknown throw") {
    throw 42;
}

TEST_CASE("skip: conditional") {
    SKIP("self-test skip reason");
}
