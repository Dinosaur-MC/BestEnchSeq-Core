// Test-framework self-test: registration / arg parsing / exception fallback /
// SKIP. The ctest registration only runs --filter "positive:" (exit 0);
// negative paths are verified manually per the plan.
// 注意：#define 必须在唯一的 include 之前（pragma once 会让第二次 include 空转）。
#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include <condition_variable>
#include <mutex>
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

TEST_CASE_TIMEOUT("timeout: infinite loop", 1) {
    for (;;);  // 忙循环：验证杀线程路径（async cancel / TerminateThread）
}

TEST_CASE_TIMEOUT("timeout: deadlock", 1) {
    // 确定阻塞（各平台一致）：条件变量永不被唤醒。注：非递归互斥量二次加锁在
    // MSVC 上抛异常而非死锁，故用 cv.wait 构造真阻塞，验证阻塞中杀线程路径。
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);
}

TEST_CASE("positive: after kill") {
    // 注册在超时负用例之后：验证 OS 杀线程后进程健康、后续 case 仍能运行。
    expect(true, "process healthy after thread kill");
}
