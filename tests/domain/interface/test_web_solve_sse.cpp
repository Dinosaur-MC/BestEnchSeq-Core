// tests/domain/interface/test_web_solve_sse.cpp
// SseHub 发布/订阅路由 + WebSolveService 可选 SseHub 出口（向后兼容）。
#define BESQ_TEST_MAIN
#include "domain/interface/web/SseHub.h"
#include "domain/interface/web/WebSolveService.h"
#include "framework/test_framework.h"
#include <atomic>
#include <string>
#include <thread>

using namespace web;

// SseHub 独立测试：task → 订阅者帧路由
static void test_hub() {
    SseHub hub;
    std::atomic<int> frames{0};
    std::string got;
    auto sub = hub.subscribe("task-1", [&](const std::string& id, std::string frame) {
        ++frames;
        got = id + ":" + frame;
    });
    hub.publish("task-1", "data: x\n\n");
    expect(frames.load() == 1 && got == "task-1:data: x\n\n", "routed to subscriber");
    hub.unsubscribe("task-1", sub);
    hub.publish("task-1", "data: y\n\n");
    expect(frames.load() == 1, "no frame after unsubscribe");
}

// 多订阅者广播 + unsubscribe_all + subscriber_count
static void test_multiple_subscribers() {
    SseHub hub;
    std::atomic<int> a{0}, b{0};
    hub.subscribe("t", [&](const std::string&, std::string) { ++a; });
    hub.subscribe("t", [&](const std::string&, std::string) { ++b; });
    expect(hub.subscriber_count("t") == 2, "two subscribers counted");
    hub.publish("t", "data: z\n\n");
    expect(a.load() == 1 && b.load() == 1, "broadcast to all subscribers");
    hub.unsubscribe_all("t");
    hub.publish("t", "data: z\n\n");
    expect(a.load() == 1 && b.load() == 1, "no frame after unsubscribe_all");
    expect(hub.subscriber_count("t") == 0, "count zero after unsubscribe_all");
}

// 未知 task publish 为空操作，不影响其他订阅
static void test_unknown_task_publish_is_noop() {
    SseHub hub;
    std::atomic<int> a{0};
    hub.subscribe("t1", [&](const std::string&, std::string) { ++a; });
    hub.publish("no-such-task", "data: q\n\n");
    expect(a.load() == 0, "no delivery to unrelated task");
}

TEST_CASE("test_web_solve_sse") {
    test_hub();
    test_multiple_subscribers();
    test_unknown_task_publish_is_noop();
    TEST_PASS("web solve sse hub");
}
