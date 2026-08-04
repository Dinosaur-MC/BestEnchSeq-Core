// =============================================================================
// WebSolveService + /api/calculator lifecycle tests.
// =============================================================================
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/WebSchema.h"
#include "domain/interface/BesqContext.h"
#include "framework/test_utils.h"
#include <atomic>
#include <chrono>
#include <thread>

static WebTaskDto make_task() {
    WebTaskDto dto;
    dto.target.item = "diamond_sword";
    dto.target.enchants = {{"sharpness", 5}};
    dto.algorithm = "dp_merge";
    dto.max_solutions = 1;
    return dto;
}

void test_lifecycle_complete() {
    BesqContext ctx;
    ctx.load_builtin();
    webhttp::WebSolveService svc(ctx);

    auto id = svc.start(make_task());
    expect(!id.empty(), "start returns a task id");

    // Poll to completion (with a generous bound).
    bool done = false;
    for (int i = 0; i < 500; ++i) {
        auto s = svc.status(id);
        if (s.state == webhttp::TaskState::Completed) {
            done = true;
            expect(!s.result.empty(), "completed task carries formatted JSON result");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(done, "task reaches Completed");

    // Unknown task id → status throws WebHttpError 404.
    expect_throws_as<webhttp::WebHttpError>([&] {
        svc.status("nope");
    }, "unknown task id throws 404");
    TEST_PASS("calculator lifecycle complete");
}

void test_single_active_slot() {
    BesqContext ctx;
    ctx.load_builtin();
    webhttp::WebSolveService svc(ctx);

    // Seed a large target so the first solve stays running.
    for (int i = 0; i < 18; ++i) {
        EnchInfo info;
        info.id = NSID("test:e_" + std::to_string(i));
        info.name = "E " + std::to_string(i);
        info.max_level = 5;
        info.multiplier = 1;
        info.supported_items.insert(NSID("#minecraft:swords"));
        expect(ctx.add_enchantment(info), "seed ench " + std::to_string(i));
    }
    WebTaskDto big;
    big.target.item = "diamond_sword";
    big.algorithm = "dp_merge";
    for (int i = 0; i < 18; ++i)
        big.target.enchants.push_back({"test:e_" + std::to_string(i), 5});

    auto id1 = svc.start(big);
    expect(!id1.empty(), "first task starts");

    // Second start while the first is Running → 409.
    expect_throws_as<webhttp::WebHttpError>([&] {
        svc.start(make_task());
    }, "second start while active throws 409");

    // Cancel the first, then a new start succeeds.
    expect(svc.cancel(id1), "cancel active task");
    bool cancelled = false;
    for (int i = 0; i < 100; ++i) {
        auto s = svc.status(id1);
        if (s.state == webhttp::TaskState::Cancelled ||
            s.state == webhttp::TaskState::Failed) { cancelled = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(cancelled, "cancelled task observed as Cancelled/Failed");

    auto id2 = svc.start(make_task());
    expect(!id2.empty(), "new start after cancel succeeds");
    TEST_PASS("calculator single active slot");
}

int main() {
    try {
        test_lifecycle_complete();
        test_single_active_slot();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
