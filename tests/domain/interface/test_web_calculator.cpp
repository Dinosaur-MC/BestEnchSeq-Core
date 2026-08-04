// =============================================================================
// WebSolveService + /api/calculator lifecycle tests.
// =============================================================================
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/WebSchema.h"
#include "domain/interface/BesqContext.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include "common/io/json.h"
#include "domain/interface/web/resources/ApiCalculator.h"
#include "framework/test_utils.h"
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

void test_failed_and_has_active() {
    BesqContext ctx;
    ctx.load_builtin();
    webhttp::WebSolveService svc(ctx);

    // Unknown enchantment → task fails with a non-empty error.
    WebTaskDto bad;
    bad.target.item = "diamond_sword";
    bad.target.enchants = {{"nonexistent_ench", 1}};
    bad.algorithm = "dp_merge";
    auto id = svc.start(bad);
    bool failed = false;
    for (int i = 0; i < 200; ++i) {
        auto s = svc.status(id);
        if (s.state == webhttp::TaskState::Failed) {
            failed = true;
            expect(!s.error.empty(), "failed task carries an error message");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(failed, "invalid task reaches Failed");
    expect(!svc.has_active(), "has_active false after task completes");

    // Start a good task → has_active true while running.
    auto id2 = svc.start([] {
        WebTaskDto dto;
        dto.target.item = "diamond_sword";
        dto.target.enchants = {{"sharpness", 5}};
        dto.algorithm = "dp_merge";
        dto.max_solutions = 1;
        return dto;
    }());
    // The solve may race ahead of the main thread, so poll until the Running
    // state is observed (a dp_merge on a small target can finish in microseconds).
    bool saw_active = false;
    for (int i = 0; i < 200 && !saw_active; ++i) {
        saw_active = svc.has_active();
        if (!saw_active) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(saw_active, "has_active true while a task runs");
    // Wait for it to finish, then has_active is false again.
    for (int i = 0; i < 500 && svc.has_active(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(!svc.has_active(), "has_active false after running task completes");
    (void)id2;
    TEST_PASS("calculator failed path + has_active");
}

void test_calculator_resource() {
    BesqContext ctx;
    ctx.load_builtin();
    webhttp::WebSolveService svc(ctx);

    // POST with a valid task → {task_id}
    auto post = Json::parse(ApiCalculator::handle_post(svc, R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"sharpness","level":5}]},
        "algorithm":"dp_merge",
        "max_solutions":1
    })"));
    expect(post["task_id"].type() == JsonType::String, "POST returns a task_id");
    std::string id = post["task_id"].as<std::string>();

    // GET while running or completed; eventually completed with result.
    bool completed = false;
    bool saw_running = false;
    for (int i = 0; i < 500; ++i) {
        auto st = Json::parse(ApiCalculator::handle_get(svc, id));
        if (st["state"].as<std::string>() == "running" && !saw_running) {
            saw_running = true;
            expect(st["progress"].type() == JsonType::Number,
                   "running envelope carries numeric progress");
        }
        if (st["state"].as<std::string>() == "completed") {
            completed = true;
            expect(st["result"]["success"] == true, "result JSON has success true");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(completed, "calculator task completes");

    // DELETE on the finished task is a no-op success envelope.
    auto del = Json::parse(ApiCalculator::handle_del(svc, id));
    expect(del["ok"] == true, "DELETE returns ok");

    // Unknown task → 404.
    expect_throws_as<webhttp::WebHttpError>([&] {
        ApiCalculator::handle_get(svc, "nope");
    }, "unknown task id throws 404");

    // Failed envelope via the resource: an unknown enchantment fails the task.
    auto bad_post = Json::parse(ApiCalculator::handle_post(svc, R"({
        "target": {"item":"diamond_sword","enchants":[{"id":"nonexistent_ench","level":1}]},
        "algorithm":"dp_merge"
    })"));
    std::string bad_id = bad_post["task_id"].as<std::string>();
    bool failed = false;
    for (int i = 0; i < 200; ++i) {
        auto st = Json::parse(ApiCalculator::handle_get(svc, bad_id));
        if (st["state"].as<std::string>() == "failed") {
            failed = true;
            expect(st.has("error") && st["error"].as<std::string>() != "",
                   "failed envelope carries error");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(failed, "invalid task reaches failed envelope");
    TEST_PASS("calculator resource lifecycle");
}

int main() {
    // The Failed path surfaces a localized error message (tr_fmt), which needs
    // the translation tables registered (the real app does this in main()).
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");
    try {
        test_lifecycle_complete();
        test_single_active_slot();
        test_failed_and_has_active();
        test_calculator_resource();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
