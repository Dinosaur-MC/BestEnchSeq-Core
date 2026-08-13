// =============================================================================
// End-to-end: real HttpServer + WebModule + BesqContext over localhost.
//
// Drives the full §12.1 endpoint-coverage matrix over REAL sockets — no direct
// dispatch, no mocks. The transport path exercised is:
//   raw HTTP bytes → poller → Reactor(EventLoop) → Connection → HttpParser →
//   WebModule.dispatch → controllers → bytes back out the same wire.
//
// Cases:
//  1. GET /                      → 307 + Location: /public/index.html
//  2. GET /public/index.html     → 200 text/html, embedded SPA body
//  3. GET /health, /api/status, /api/settings → 200 + field assertions
//  4. GET /api/profiles (+ meta) → profiles/active + full metadata fields
//  5. POST /api/tasks → 202+task_id+Location → poll to completed+result
//  5b. Failing task (unknown enchant) → snapshot reaches state=failed+error
//      (the SSE failed FRAME is deliberately not asserted live — see the
//      comment in test_task_failed_snapshot; hub-level shape is pinned in
//      test_web_api)
//  6. SSE over the real socket   → GET /api/tasks/{id}/events: stream head +
//     a live `event: completed` + `data:` frame (Reactor→Connection→wire)
//  6b. close-storm SSE regression → 32 rapid connect/close cycles while an SSE
//     stream is open; the completed frame must still arrive within budget
//     (pins the poller lock-starvation fix, see the case comment)
//  7. Error envelope             → 404 {error,code}; 405 + Allow
//  8. Concurrent clients         → 4 threads each GET /health → all 200
//  9. Path traversal             → GET /public/../secret → 404
// 10. /api/history               → 普通查询 + 分页（offset/limit）+ 游标（after_seq）
//      + Completed 事件字段完整性；/api/logs* → 404（端点已删，替代原 logs SSE 用例）
//
// All waits are bounded loops; nothing can hang the suite indefinitely.
// =============================================================================

#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Socket.h"
#include "domain/interface/web/WebModule.h"
#define BESQ_TEST_MAIN

#include "framework/test_framework.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace web;

namespace {

constexpr const char* kIndexHtml = "<h1>hi</h1>";

// ---------------------------------------------------------------------------
// Raw-socket helpers
// ---------------------------------------------------------------------------

/// One request/response exchange on a fresh connection (blocking recv).
/// Response bodies here are small JSON/static assets, so a single recv with a
/// bounded timeout captures the full reply.
/// flake 修复 P0-2：冷启动/负载窗口内单次 3s recv 可能超时拿空——对空响应
/// 有界重试一次（对齐 test_concurrent_clients 的既有模式，覆盖全部调用点）。
std::string http_exchange(HttpServer& server, const std::string& raw) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        int c = sock_connect("127.0.0.1", server.port());
        if (c < 0)
            continue;
        sock_send(c, raw, 3000);
        std::string body;
        sock_recv(c, body, 64 * 1024, 3000);
        sock_close(c);
        if (!body.empty())
            return body;
    }
    return "";
}

/// Non-blocking recv loop: accumulate `got` until `needle` shows up or we run
/// out of tries. Used for the open-ended SSE stream (no Content-Length, the
/// server keeps the connection open).
void recv_until(int client, std::string& got, const char* needle, int max_tries) {
    for (int i = 0; i < max_tries && got.find(needle) == std::string::npos; ++i) {
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0)
            got += c;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

/// 从完整 HTTP 响应（头 + 体）中切出 JSON body（http_exchange 返回原始字节流，
/// 含响应头——需要结构化断言时先切 body 再 Json::parse）。
std::string response_body(const std::string& resp) {
    const auto sep = resp.find("\r\n\r\n");
    return sep == std::string::npos ? std::string() : resp.substr(sep + 4);
}

/// Pull `"task_id":"..."` out of a 202 response body (tolerant of the compact
/// serializer's exact spacing: finds the key, then the quoted value).
std::string extract_task_id(const std::string& resp) {
    const auto tid = resp.find("\"task_id\"");
    if (tid == std::string::npos)
        return "";
    const auto colon = resp.find(':', tid);
    if (colon == std::string::npos)
        return "";
    const auto open = resp.find('"', colon);
    if (open == std::string::npos)
        return "";
    const auto close = resp.find('"', open + 1);
    if (close == std::string::npos)
        return "";
    return resp.substr(open + 1, close - open - 1);
}

/// POST a task body and return the full HTTP response.
std::string post_task(HttpServer& server, const std::string& body) {
    std::string req = "POST /api/tasks HTTP/1.1\r\nHost: x\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " +
                      std::to_string(body.size()) + "\r\n\r\n" + body;
    return http_exchange(server, req);
}

/// Exchange measured against the wall clock. `elapsed_ms` receives the
/// duration of the LAST attempt — the timed budget is asserted on the
/// exchange that actually produced the body. A single bounded recv captures
/// the full small-body reply; when the server never answers (e.g. the pre-A2
/// gate-blocking behavior holds the response until the solve ends) the 3s
/// recv cap bounds the wait and the elapsed value blows past the budget →
/// the assertion fails.
///
/// flake 修复（P0-2 同款）：负载窗口内单次 recv 可能超时拿空——空响应有界
/// 重试一次。计时重置：只测本次交换的耗时（首次饥饿不算进预算）。重试
/// 不可能掩盖锁回归：修复前 gate 阻塞的服务仍在 3s recv 帽内送达。
std::string timed_exchange(HttpServer& server, const std::string& raw, int64_t& elapsed_ms) {
    std::string body;
    int64_t last_ms = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto t0 = std::chrono::steady_clock::now();
        int c = sock_connect("127.0.0.1", server.port());
        if (c < 0)
            continue;
        sock_send(c, raw, 3000);
        sock_recv(c, body, 64 * 1024, 3000);
        sock_close(c);
        last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (!body.empty())
            break;
    }
    elapsed_ms = last_ms;
    return body;
}

/// Fetch one task-status snapshot in FULL. The server keeps connections alive
/// (idle keep-alive 30s), so "response done" must be detected via
/// Content-Length rather than a bare recv; a large completed result (heavy
/// bb_dp solution) needs multiple recv_nb rounds. Bounded by `deadline_ms`;
/// returns "" if the deadline hits before the full body arrives.
std::string fetch_status(HttpServer& server, const std::string& id, int64_t deadline_ms) {
    int c = sock_connect("127.0.0.1", server.port());
    if (c < 0)
        return "";
    sock_send(c, "GET /api/tasks/" + id + " HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    set_nonblocking(c);
    std::string got;
    size_t header_end = std::string::npos;
    size_t content_length = std::string::npos;
    const auto t_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms);
    while (std::chrono::steady_clock::now() < t_end) {
        std::string chunk;
        const int n = sock_recv_nb(c, chunk, 65536);
        if (n > 0) {
            got += chunk;
            if (header_end == std::string::npos && (header_end = got.find("\r\n\r\n")) != std::string::npos) {
                const size_t cl = got.find("Content-Length:");
                if (cl != std::string::npos && cl < header_end)
                    content_length = static_cast<size_t>(std::strtoull(got.c_str() + cl + 15, nullptr, 10));
            }
            if (header_end != std::string::npos && content_length != std::string::npos &&
                got.size() >= header_end + 4 + content_length)
                break;
        } else if (n < 0) {
            break; // 对端 FIN（-2）/错误（-1）：连接已关闭，继续读无意义
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    sock_close(c);
    return got;
}

/// Repeatedly fetch task status until `needle` shows up or the deadline
/// passes; returns the last fetched body (a completed body carries the result).
std::string poll_status_until(HttpServer& server, const std::string& id, const char* needle, int64_t deadline_ms) {
    std::string body;
    const auto t_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms);
    do {
        body = fetch_status(server, id, 5000);
        if (body.find(needle) != std::string::npos)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < t_end);
    return body;
}

// ---------------------------------------------------------------------------
// Case 1+2: / redirect + /public static asset
// ---------------------------------------------------------------------------
void test_static_and_root(HttpServer& server) {
    // GET / → 307 + Location: /public/index.html
    auto root = http_exchange(server, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(root.find("307") != std::string::npos, "root redirects 307");
    expect(root.find("Location: /public/index.html") != std::string::npos, "root Location header");

    // GET /public/index.html → 200 text/html, embedded body
    auto idx = http_exchange(server, "GET /public/index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(idx.find("200 OK") != std::string::npos, "index serves 200");
    expect(idx.find("Content-Type: text/html") != std::string::npos, "index content-type text/html");
    expect(idx.find(kIndexHtml) != std::string::npos, "index body served");
}

// ---------------------------------------------------------------------------
// Case 3: health / status / settings
// ---------------------------------------------------------------------------
void test_health_status_settings(HttpServer& server) {
    auto health = http_exchange(server, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(health.find("200 OK") != std::string::npos, "health responds 200");
    expect(health.find("\"status\":\"ok\"") != std::string::npos, "health status field");

    auto status = http_exchange(server, "GET /api/status HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(status.find("200 OK") != std::string::npos, "status responds 200");
    expect(status.find("active_profile") != std::string::npos, "status active_profile");
    expect(status.find("profile_count") != std::string::npos, "status profile_count");
    expect(status.find("algorithm_count") != std::string::npos, "status algorithm_count");
    expect(status.find("has_active_solve") != std::string::npos, "status has_active_solve");
    expect(status.find("uptime_ms") != std::string::npos, "status uptime_ms");

    auto settings = http_exchange(server, "GET /api/settings HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(settings.find("200 OK") != std::string::npos, "settings responds 200");
    expect(settings.find("lang") != std::string::npos, "settings lang field");
    expect(settings.find("log_level") != std::string::npos, "settings log_level field");
}

// ---------------------------------------------------------------------------
// Case 4: profile list + metadata
// ---------------------------------------------------------------------------
void test_profiles(HttpServer& server) {
    auto prof = http_exchange(server, "GET /api/profiles HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(prof.find("200 OK") != std::string::npos, "profiles list 200");
    expect(prof.find("builtin:vanilla") != std::string::npos, "profiles list contains builtin:vanilla");
    expect(prof.find("\"active\"") != std::string::npos, "profiles active field");

    auto meta = http_exchange(server, "GET /api/profiles/builtin:vanilla HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(meta.find("200 OK") != std::string::npos, "profile metadata 200");
    expect(meta.find("\"name\"") != std::string::npos, "meta name");
    expect(meta.find("\"dependencies\"") != std::string::npos, "meta dependencies");
    expect(meta.find("\"version\"") != std::string::npos, "meta version");
    expect(meta.find("\"release_tag\"") != std::string::npos, "meta release_tag");
    expect(meta.find("\"is_root\"") != std::string::npos, "meta is_root");
    expect(meta.find("\"ench_count\"") != std::string::npos, "meta ench_count");
    expect(meta.find("\"eq_count\"") != std::string::npos, "meta eq_count");
    expect(meta.find("\"tag_count\"") != std::string::npos, "meta tag_count");
    expect(meta.find("\"format\"") != std::string::npos, "meta format");
}

// ---------------------------------------------------------------------------
// Case 4b: GET /api/profiles/{key}/enchantables/{item} over the real socket.
// ---------------------------------------------------------------------------
void test_enchantables(HttpServer& server) {
    auto sw = http_exchange(server, "GET /api/profiles/builtin:vanilla/enchantables/minecraft:diamond_sword "
                                    "HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(sw.find("200 OK") != std::string::npos, "enchantables responds 200");
    expect(sw.find("minecraft:sharpness") != std::string::npos, "diamond_sword enchantables contains sharpness");
    expect(sw.find("minecraft:efficiency") == std::string::npos, "diamond_sword enchantables excludes efficiency");
}

// ---------------------------------------------------------------------------
// Case 5: POST /api/tasks → 202 + task_id + Location → poll to completed
// ---------------------------------------------------------------------------
void test_task_submit_and_poll(HttpServer& server) {
    const std::string body = "{\"target\":{\"item\":\"diamond_sword\",\"enchants\":"
                             "[{\"id\":\"sharpness\",\"level\":5}]},\"algorithm\":\"dp_merge\","
                             "\"max_solutions\":1}";
    auto cpost = post_task(server, body);

    expect(cpost.find("202") != std::string::npos, "task submit returns 202 Accepted");
    expect(cpost.find("Location: /api/tasks/") != std::string::npos, "task Location header");
    const std::string id = extract_task_id(cpost);
    expect(!id.empty(), "task id extracted");

    // Bounded poll: up to 50 × 100ms = 5s. The dp_merge solve on this light
    // target completes in well under a second, so this usually exits after a
    // couple of iterations.
    bool completed = false;
    for (int i = 0; i < 50 && !completed; ++i) {
        auto st = http_exchange(server, "GET /api/tasks/" + id + " HTTP/1.1\r\nHost: x\r\n\r\n");
        if (st.find("\"state\":\"completed\"") != std::string::npos) {
            completed = true;
            expect(st.find("result") != std::string::npos, "completed task has result");
        } else if (st.find("\"state\":\"failed\"") != std::string::npos) {
            break; // surfaced by the completed assertion below
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    expect(completed, "task completes end-to-end");
}

// ---------------------------------------------------------------------------
// Case 5b: deterministically failing task → `state=failed` snapshot over the
// wire. An unknown enchantment id throws inside the worker's build_request
// (before solve), so the failure is deterministic and fast; the snapshot is
// race-free because a finished task stays in the table until the next submit
// reaps it.
//
// NOTE — the §12.1 "SSE failed frame on the wire" row is intentionally NOT
// asserted as a live socket frame:
//   * every deterministic failure (unknown ench/equip/algorithm, mode
//     mismatch) throws inside the worker within ~100µs of submission — far
//     before a second TCP connection can register an SSE subscription (the
//     poller's select cadence alone is ~100ms), so the failed frame is always
//     published to an empty subscriber set. A bounded wait would be a flaky
//     race, not a test.
//   * a conflicting-enchantment target does NOT fail: solve() returns
//     success=false with empty solutions, which the web service formats as a
//     COMPLETED result carrying success:false (only the CLI maps that to an
//     error). So "impossible target" cannot produce a failed frame either.
//   The failed frame's byte format is instead pinned deterministically at the
//   hub level in test_web_api (test_failed_frame_shape), and the worker's real
//   failure path is covered here by the snapshot below.
// ---------------------------------------------------------------------------
void test_task_failed_snapshot(HttpServer& server) {
    const std::string body = "{\"target\":{\"item\":\"diamond_sword\",\"enchants\":"
                             "[{\"id\":\"no_such_ench_xyz\",\"level\":1}]},\"algorithm\":\"dp_merge\"}";
    auto resp = post_task(server, body);
    expect(resp.find("202") != std::string::npos, "failing task accepted 202");
    const std::string id = extract_task_id(resp);
    expect(!id.empty(), "failing task id extracted");

    bool failed = false;
    for (int i = 0; i < 50 && !failed; ++i) {
        auto st = http_exchange(server, "GET /api/tasks/" + id + " HTTP/1.1\r\nHost: x\r\n\r\n");
        if (st.find("\"state\":\"failed\"") != std::string::npos) {
            failed = true;
            expect(st.find("\"error\"") != std::string::npos, "failed snapshot carries error field");
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    expect(failed, "failing task snapshot reaches state=failed");
}

// ---------------------------------------------------------------------------
// Case 6: SSE over the real socket
//
// The worker publishes a `progress` frame before solve() and a `completed`
// frame after it. To observe `completed` on the wire the SSE subscription must
// be registered before the solve finishes. We therefore use a moderately heavy
// direct-mode target (chestplate, 4 enchants from 2 source enchants) whose
// dp_merge solve takes on the order of a hundred milliseconds — the SSE
// connect + subscribe lands in ~1ms, so the subscription is guaranteed to be in
// place long before the completed publish. The stream head (200 + text/event-
// stream) and the completed frame are both read from the live socket.
void test_sse_events(HttpServer& server) {
    // 根治后（SseHub 每任务保留最后一帧，迟到订阅者 subscribe 即重放）：
    // 订阅晚于 completed 发布不再丢帧——小 solve 即可，窗口无关。
    const std::string body = "{\"target\":{\"item\":\"diamond_chestplate\",\"enchants\":["
                             "{\"id\":\"protection\",\"level\":4},{\"id\":\"thorns\",\"level\":3},"
                             "{\"id\":\"unbreaking\",\"level\":3},{\"id\":\"mending\",\"level\":1}]},"
                             "\"source\":[{\"id\":\"protection\",\"level\":3},"
                             "{\"id\":\"thorns\",\"level\":2}],\"algorithm\":\"dp_merge\"}";
    auto resp = post_task(server, body);
    const std::string id = extract_task_id(resp);
    expect(!id.empty(), "SSE task created");

    // Fresh client connection for the open-ended stream.
    int c = sock_connect("127.0.0.1", server.port());
    expect(c >= 0, "SSE client connects");
    sock_send(c, "GET /api/tasks/" + id + "/events HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    set_nonblocking(c);

    std::string got;
    // Stream head: HTTP/1.1 200 + Content-Type: text/event-stream. Arrives
    // within a few ms of connect; 2s budget is generous.
    recv_until(c, got, "text/event-stream", 200);
    expect(got.find("HTTP/1.1 200 OK") != std::string::npos, "SSE stream head status 200");
    expect(got.find("text/event-stream") != std::string::npos, "SSE content-type");
    expect(got.find("Content-Length") == std::string::npos, "SSE head has no Content-Length (open-ended stream)");

    // Completed frame: solve 快（dp_merge 小目标 ~100ms），订阅建立或在其
    // 后——SseHub 重放保证终态帧必达；3s 预算宽裕。
    recv_until(c, got, "event: completed", 300);
    const auto pos = got.find("event: completed");
    expect(pos != std::string::npos, "SSE completed event frame on the wire");
    if (pos != std::string::npos) {
        expect(got.find("data: ", pos) != std::string::npos, "SSE completed frame carries a data payload");
    }

    sock_close(c);
}

// ---------------------------------------------------------------------------
// Case 6b (P3 回归): close-storm 下 SSE 帧必须准时送达。
//
// 根因（2026-08-09）：poller 全程持 pmutex 跨 select()；短连接风暴（快速
// connect→close）在对端留下未处理的 FIN，select 每轮立即就绪，poller 以微秒
// 间隔连续重锁（自旋），Reactor 线程的 unregister_fd/set_fd_interest 在 futex
// 唤醒竞速中连续落败 → 被饿死数秒（WSL 实测 ≥3s），SSE 帧（排队在饿死的
// drive 之后）整体冻结、客户端 3s 内收不到终态帧（旧代码 WSL 5/5 失败）。
// 修复：注册表写操作改事件队列（loop 线程推事件、poller 每轮 drain 独占应用），
// pmutex 整体删除；socket 关闭延迟到 poller 线程（close_queue drain），快照 fd
// 永不提前关闭。
//
// 本用例把回归钉死：SSE 流开启期间连续开关 32 条短连接（每条约 1 个 FIN
// 进入 poller 的 select 集合——正是旧实现的饿死触发器），终态帧必须在 2s
// 预算内到达（修复后 ~200ms；饿死时 >3s，用例超时即失败）。
void test_sse_under_close_storm(HttpServer& server) {
    const std::string body = "{\"target\":{\"item\":\"diamond_chestplate\",\"enchants\":["
                             "{\"id\":\"protection\",\"level\":4},{\"id\":\"thorns\",\"level\":3},"
                             "{\"id\":\"unbreaking\",\"level\":3},{\"id\":\"mending\",\"level\":1}]},"
                             "\"source\":[{\"id\":\"protection\",\"level\":3},"
                             "{\"id\":\"thorns\",\"level\":2}],\"algorithm\":\"dp_merge\"}";
    auto resp = post_task(server, body);
    const std::string id = extract_task_id(resp);
    expect(!id.empty(), "storm task created");

    int c = sock_connect("127.0.0.1", server.port());
    expect(c >= 0, "storm SSE client connects");
    sock_send(c, "GET /api/tasks/" + id + "/events HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    set_nonblocking(c);

    std::string got;
    recv_until(c, got, "text/event-stream", 200);
    expect(got.find("text/event-stream") != std::string::npos, "storm stream head");

    // Close-storm：32 条快速 connect→send→close（SseHub 订阅保持开放）。
    for (int i = 0; i < 32; ++i) {
        int h = sock_connect("127.0.0.1", server.port());
        if (h < 0)
            continue;
        sock_send(h, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n", 1000);
        std::string buf;
        sock_recv(h, buf, 4096, 1000);
        sock_close(h);
    }

    // 终态帧必须准时到达（2s 预算；solve ~100ms + 风暴 ~100ms 后余量充足）。
    recv_until(c, got, "event: completed", 200);
    expect(got.find("event: completed") != std::string::npos, "SSE completed frame delivered during close-storm");
    sock_close(c);
}

// ---------------------------------------------------------------------------
// Case 7: error envelope (404 + 405/Allow)
// ---------------------------------------------------------------------------
void test_error_envelopes(HttpServer& server) {
    auto nope = http_exchange(server, "GET /nope HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(nope.find("404") != std::string::npos, "unknown path returns 404");
    expect(nope.find("\"error\"") != std::string::npos, "404 error envelope");
    expect(nope.find("\"code\"") != std::string::npos, "404 error code field");

    auto del = http_exchange(server, "DELETE /api/settings HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(del.find("405") != std::string::npos, "DELETE settings returns 405");
    expect(del.find("Allow:") != std::string::npos, "405 Allow header present");
}

// ---------------------------------------------------------------------------
// Case 8: concurrent clients — 4 threads each GET /health → all 200
//
// The server is single-threaded (one Reactor loop), so under a loaded Debug
// build a burst of 4 connections can exceed a single 3s recv timeout — a
// scheduling flake, not a correctness failure. Each client therefore retries
// the exchange (bounded) until it gets a 200 or exhausts 3 attempts; the
// assertion still requires all 4 clients to succeed.
// ---------------------------------------------------------------------------
void test_concurrent_clients(HttpServer& server) {
    std::atomic<int> ok{0};
    std::vector<std::thread> clients;
    for (int i = 0; i < 4; ++i) {
        clients.emplace_back([&] {
            for (int attempt = 0; attempt < 3 && ok.load() < 4; ++attempt) {
                int c = sock_connect("127.0.0.1", server.port());
                if (c < 0)
                    continue;
                sock_send(c, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
                std::string got;
                sock_recv(c, got, 16 * 1024, 3000);
                bool good = got.find("200 OK") != std::string::npos && got.find("status") != std::string::npos;
                sock_close(c);
                if (good) {
                    ++ok;
                    break;
                }
            }
        });
    }
    for (auto& t : clients)
        t.join();
    expect(ok.load() == 4, "all 4 concurrent clients got a 200 response");
}

// ---------------------------------------------------------------------------
// Case 9: path traversal under /public
// ---------------------------------------------------------------------------
void test_path_traversal(HttpServer& server) {
    auto tr = http_exchange(server, "GET /public/../secret HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(tr.find("404") != std::string::npos, "traversal request blocked 404");
    expect(tr.find("secret") == std::string::npos, "no file content leaked");
}

// ---------------------------------------------------------------------------
// Case 10 (I-3): stalled-read timeout over the real sweep chain.
//
// A client sends a partial request ("GET /api/status HT" — header terminator
// missing → parser Incomplete → Connection::_partial) and then stalls. The
// poller sweep (~1s cadence) → Reactor::check_timeout → sweep_check must close
// it after the 5s slow-read cap. The client observes a clean EOF with NO
// response bytes (bounded wait ~10s: 5s cap + sweep cadence + margins).
// ---------------------------------------------------------------------------
void test_slow_client_timeout(HttpServer& server) {
    int c = sock_connect("127.0.0.1", server.port());
    expect(c >= 0, "slow client connects");
    set_nonblocking(c);
    // 头未终结的半请求（无 \r\n\r\n → 解析器恒为 Incomplete → _partial=true）。
    expect(sock_send(c, "GET /api/status HT", 3000), "partial request sent");

    bool eof = false;
    // flake 修复 P0-1：固定 1000×10ms 轮询改截止时间（20s 预算——名义 5-6s
    // 关闭 + 主机调度抖动余量，通过运行时长不变，失败时才多等）；EOF 判定
    // n <= 0（0 = FIN；-1 = RST/错误，连接同样已关闭，此前被当"继续等"）。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    int stray = 0;
    while (std::chrono::steady_clock::now() < deadline && !eof) {
        if (wait_readable(c, 0) == 1) {
            std::string chunk;
            const int n = sock_recv_nb(c, chunk, 4096);
            if (n <= 0)
                eof = true;
            else
                stray += n;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (!eof)
        std::fprintf(stderr, "[DIAG-WIN] slow_client no EOF, stray=%d\n", stray);
    expect(eof, "server closed the stalled partial-request connection (5s cap)");
    sock_close(c);
}

// ---------------------------------------------------------------------------
// Case 11 (I-1): /api/history 端到端——真实 socket 上普通查询 + 分页 + 游标 +
// Completed 事件字段完整性；/api/logs* 已删除 → 404。
//
// 前置先跑一个已完成任务（与 test_task_submit_and_poll 同 body 与轮询），保证
// 存在带成本字段的 Completed 事件。事件记录在状态字提交之后立即进行——状态
// 可观察与事件可查询间有微小窗口，用有界重试吸收（断言全在循环外，计数恒定）。
// ---------------------------------------------------------------------------
void test_history_endpoints(HttpServer& server) {
    const std::string body = "{\"target\":{\"item\":\"diamond_sword\",\"enchants\":"
                             "[{\"id\":\"sharpness\",\"level\":5}]},\"algorithm\":\"dp_merge\","
                             "\"max_solutions\":1}";
    auto cpost = post_task(server, body);
    expect(cpost.find("202") != std::string::npos, "history task submit 202");
    const std::string id = extract_task_id(cpost);
    expect(!id.empty(), "history task id extracted");
    bool completed = false;
    for (int i = 0; i < 50 && !completed; ++i) {
        auto st = http_exchange(server, "GET /api/tasks/" + id + " HTTP/1.1\r\nHost: x\r\n\r\n");
        if (st.find("\"state\":\"completed\"") != std::string::npos)
            completed = true;
        else if (st.find("\"state\":\"failed\"") != std::string::npos)
            break; // surfaced by the completed assertion below
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(completed, "history task completes end-to-end");

    // 1. 普通查询 → 200；body 含 events 数组与 total（≥ 本次任务的 2 条事件）。
    auto h = http_exchange(server, "GET /api/history HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(h.find("200 OK") != std::string::npos, "history responds 200");
    expect(h.find("\"events\"") != std::string::npos, "history carries events array");
    expect(h.find("\"total\"") != std::string::npos, "history carries total");
    auto hj = Json::parse(response_body(h));
    expect(hj["total"].as<int64_t>() >= 2, "history total covers the task events");
    expect(!hj["events"].as_array().empty(), "history events non-empty");

    // 2. 分页：offset=0&limit=1 → 恰 1 条 + next_offset=1；最新一条必为本任务
    //    Completed（有界重试直到事件记录落地；断言在循环外）。
    int64_t seq0 = -1;
    bool page_ok = false, first_completed = false, cost_fields = false;
    for (int i = 0; i < 50 && !page_ok; ++i) {
        auto pg = http_exchange(server, "GET /api/history?offset=0&limit=1 HTTP/1.1\r\nHost: x\r\n\r\n");
        if (pg.find("200 OK") == std::string::npos)
            continue;
        auto pj = Json::parse(response_body(pg));
        auto pev = pj["events"].as_array();
        if (pev.size() != 1 || !pev[0].has("task_id") || pev[0]["task_id"].as<std::string>() != id)
            continue; // 事件记录未落地：最新一条还是更早任务的事件
        seq0 = pev[0]["seq"].as<int64_t>();
        if (pev[0].has("type") && pev[0]["type"].as<std::string>() == "completed") {
            first_completed = true;
            cost_fields = pev[0].has("total_level_cost") && pev[0]["total_level_cost"].as<int64_t>() > 0 &&
                          pev[0].has("computation_ms") && pev[0]["computation_ms"].as<int64_t>() >= 0;
        }
        page_ok = first_completed && cost_fields && pj["next_offset"].as<int64_t>() == 1;
        if (!page_ok)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    expect(page_ok, "limit=1 page: exactly one event + next_offset=1");
    expect(first_completed, "newest history event is the completed event");
    expect(cost_fields, "completed event carries total_level_cost/computation_ms");

    // 3. 游标：after_seq=<最新一条 seq-1> → 只返回 seq > N 的事件（含该条）。
    auto cur = http_exchange(server, "GET /api/history?after_seq=" + std::to_string(seq0 - 1) + " HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(cur.find("200 OK") != std::string::npos, "history cursor 200");
    bool found = false;
    for (const auto& ev : Json::parse(response_body(cur))["events"].as_array())
        if (ev["seq"].as<int64_t>() == seq0)
            found = true;
    expect(found, "after_seq page contains the expected event");

    // 4. /api/logs* 已删除 → 404。
    auto gone = http_exchange(server, "GET /api/logs HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(gone.find("404") != std::string::npos, "/api/logs deleted 404");
    auto gone_ev = http_exchange(server, "GET /api/logs/events HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(gone_ev.find("404") != std::string::npos, "/api/logs/events deleted 404");
}

// ---------------------------------------------------------------------------
// Case 12 (I-2a): periodic progress frames during a long solve.
//
// Seeds sword-applicable enchantments via the API, submits a heavy bb_dp solve
// (per-layer progress: k*100/n) and subscribes over a real SSE socket. The
// sampler thread (200ms) must publish live progress frames DURING the solve —
// all before the terminal `event: completed` frame. Initial frame may race the
// subscription, so the assertion is: ≥1 progress frame total, then completed,
// in that byte order.
// ---------------------------------------------------------------------------
void test_sse_progress_frames(HttpServer& server) {
    // Seed sword enchantments so the solve takes seconds, not milliseconds.
    const int kSeed = 12;
    for (int i = 0; i < kSeed; ++i) {
        std::string body = R"({"id":"test:p_)" + std::to_string(i) + R"(","name":"P )" + std::to_string(i) +
                           R"(","max_level":5,"multiplier":1,"supported_items":["#minecraft:swords"]})";
        std::string req = "POST /api/profiles/builtin:vanilla/enchantments HTTP/1.1\r\nHost: x\r\n"
                          "Content-Type: application/json\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\n\r\n" + body;
        auto r = http_exchange(server, req);
        expect(r.find("201") != std::string::npos, "seeded enchantment " + std::to_string(i));
    }

    std::string target = R"({"target":{"item":"netherite_sword","enchants":[)";
    for (int i = 0; i < kSeed; ++i) {
        if (i)
            target += ",";
        target += R"({"id":"test:p_)" + std::to_string(i) + R"(","level":5})";
    }
    target += R"(]},"algorithm":"bb_dp"})";
    auto resp = post_task(server, target);
    const std::string id = extract_task_id(resp);
    expect(!id.empty(), "heavy progress task created");

    int c = sock_connect("127.0.0.1", server.port());
    expect(c >= 0, "progress SSE client connects");
    sock_send(c, "GET /api/tasks/" + id + "/events HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    set_nonblocking(c);

    std::string got;
    recv_until(c, got, "text/event-stream", 200);
    expect(got.find("HTTP/1.1 200 OK") != std::string::npos, "progress stream head 200");

    // 终态帧有界等待：bb_dp(12) Debug 下实测 ~3.5s；15s 预算宽裕。采样线程在
    // 求解期间每 ~200ms 采样、进度变化即发布——所有 progress 帧都应在 completed 前。
    recv_until(c, got, "event: completed", 1500);
    // event 行与 data 行可能分落两个 TCP 段：再收一小段确保整帧到齐后再断言。
    for (int i = 0; i < 50; ++i) {
        std::string chunk;
        if (sock_recv_nb(c, chunk, 4096) > 0)
            got += chunk;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto done = got.find("event: completed");
    expect(done != std::string::npos, "terminal completed frame on the wire");
    if (done != std::string::npos) {
        // flake 修复 P1：逐帧断言冗余且帧数随时序波动（200ms 采样网格相对
        // 求解进度漂移 ±1~2，见 WebSolveService 采样发布）——改纯计数 +
        // 一次断言。首帧先于 completed 是结构性保证（采样线程在 completed
        // 发布前 join），总断言数恒定，"Results: N passed" 可回归比对。
        size_t progress_frames = 0;
        size_t from = 0;
        while ((from = got.find("event: progress", from)) != std::string::npos) {
            ++progress_frames;
            ++from;
        }
        expect(progress_frames >= 1, "at least one live progress frame published during solve");
        expect(got.find("event: progress", 0) < done, "progress frames precede the terminal frame");
        expect(got.find("\"result\"", done) != std::string::npos, "completed frame carries result");
        expect(got.find("\"type\":\"completed\"", done) != std::string::npos, "completed frame carries the completed type");
    }
    sock_close(c);
}

// ---------------------------------------------------------------------------
// Case 13 (P0 验收——锁攻破): 长 solve（bb_dp 重目标）期间 profile 读/写不被
// solve 阻塞。
//
// 修复前（A2 前）：solve worker 持 _ctx_gate 覆盖整个 solve（秒~分钟级），
// 任何 profile 读/写都在 gate 上排队等 solve 结束。修复后：solve 跑在自包含
// SolveSnapshot 上，gate 只覆盖快照构建（µs-ms）与 format（ms）——profile
// 读/写即时完成。
//
// Seeding 与 test_sse_progress_frames 同款（12 个 POST .../enchantments），
// 但 supported_items 用 #minecraft:chest_armor：目标装备 diamond_chestplate
// 的 max_durability==528 由 completed result 端到端断言（A2 审查补充，防
// durability 回填回归——build_request 不再查注册表，durability 由 worker 在
// 快照构建后回填，断言的正是这条链路）。
//
// 写操作选 POST /api/profiles/{key}/enchantments（profile 变更类，纯内存
// 变更）而非 PATCH /api/settings：后者会把 runtime settings 持久化到
// <cwd>/config.json，弄脏工作树。
//
// 时序护栏（防假阳性）：
//   1) 读/写前先轮询确认 state=running；
//   2) 读/写后再拉一次快照确认仍 running——若 solve 恰在此窗口结束，测试
//      立即失败（测量窗口无效），而不是虚过；
//   3) 500ms 预算只负责兜住"阻塞到秒级"的旧形态（旧行为 = 等 solve 结束，
//      Windows 实测 0.9s / WSL 0.2s，均 > 500ms 才可能被预算区分）。快端下
//      预算本身不足以区分修复前后——真正的区分归功于 2) 的守卫：它证明
//      读/写确实发生在 solve 期间（此时若仍被 gate 阻塞，elapsed 必然
//      ≥ solve 时长 > 预算）。
// ---------------------------------------------------------------------------
void test_solve_does_not_block_profile() {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();
    WebModule module(ctx);
    module.set_static_resources({{"/index.html", {"text/html", kIndexHtml}}});
    HttpServer server;
    server.set_fallback([&](const HttpRequest& r) { return module.dispatch(r); });
    expect(server.start("127.0.0.1", 0), "server starts");
    std::thread srv([&] { server.run(); });
    struct Guard {
        HttpServer& s;
        std::thread& t;
        ~Guard() {
            if (t.joinable()) {
                s.stop();
                t.join();
            }
        }
    } guard{server, srv};

    // 1. seeding 12 个胸甲适用魔咒（同 test_sse_progress_frames 的 API 形状）
    //    → 重目标 bb_dp solve（12 魔咒 × 5 级；双平台实测 0.2~1.1s，足以提供
    //    读/写测量窗口，远大于 500ms 预算）。
    const int kSeed = 12;
    for (int i = 0; i < kSeed; ++i) {
        std::string body = R"({"id":"test:p_)" + std::to_string(i) + R"(","name":"P )" + std::to_string(i) +
                           R"(","max_level":5,"multiplier":1,"supported_items":["#minecraft:chest_armor"]})";
        std::string req = "POST /api/profiles/builtin:vanilla/enchantments HTTP/1.1\r\nHost: x\r\n"
                          "Content-Type: application/json\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\n\r\n" + body;
        auto r = http_exchange(server, req);
        expect(r.find("201") != std::string::npos, "seeded chestplate enchantment " + std::to_string(i));
    }

    // 2. 提交重目标 solve（direct 模式，无 source）。
    std::string target = R"({"target":{"item":"diamond_chestplate","enchants":[)";
    for (int i = 0; i < kSeed; ++i) {
        if (i)
            target += ",";
        target += R"({"id":"test:p_)" + std::to_string(i) + R"(","level":5})";
    }
    target += R"(]},"algorithm":"bb_dp"})";
    const auto t_submit = std::chrono::steady_clock::now();
    auto resp = post_task(server, target);
    expect(resp.find("202") != std::string::npos, "heavy bb_dp task accepted 202");
    const std::string id = extract_task_id(resp);
    expect(!id.empty(), "heavy task id extracted");

    // 3. 等 state=running（任务一提交即 Running；有界轮询兜底调度抖动）。
    auto running = poll_status_until(server, id, "\"state\":\"running\"", 10000);
    expect(running.find("\"state\":\"running\"") != std::string::npos, "solve running before concurrent access");

    // 读/写响应预算：旧行为等 solve 结束（秒级）→ 预算 500ms 足够区分。
    const int64_t kProfileAccessBudgetMs = 500;
    int64_t read_ms = 0, write_ms = 0;

    // a. 并发 profile 读（enchantables）——必须 <500ms 内完成。
    auto read_body = timed_exchange(server,
                                    "GET /api/profiles/builtin:vanilla/enchantables/minecraft:diamond_sword "
                                    "HTTP/1.1\r\nHost: x\r\n\r\n",
                                    read_ms);
    expect(read_body.find("200 OK") != std::string::npos, "profile read during solve returns 200");
    expect(read_ms < kProfileAccessBudgetMs,
           "profile read during solve not blocked (elapsed=" + std::to_string(read_ms) + "ms)");

    // b. 并发 profile 写（变更类操作：新增附魔，纯内存）——不被 solve 阻塞。
    std::string write_body = R"({"id":"test:p_)" + std::to_string(kSeed) + R"(","name":"P )" + std::to_string(kSeed) +
                             R"(","max_level":5,"multiplier":1,"supported_items":["#minecraft:chest_armor"]})";
    std::string write_req = "POST /api/profiles/builtin:vanilla/enchantments HTTP/1.1\r\nHost: x\r\n"
                            "Content-Type: application/json\r\nContent-Length: " +
                            std::to_string(write_body.size()) + "\r\n\r\n" + write_body;
    auto write_resp = timed_exchange(server, write_req, write_ms);
    expect(write_resp.find("201") != std::string::npos, "profile write during solve returns 201");
    expect(write_ms < kProfileAccessBudgetMs,
           "profile write during solve not blocked (elapsed=" + std::to_string(write_ms) + "ms)");

    // 测量窗口有效性护栏：写之后 solve 仍在 running → 读/写确实发生在 solve 期间。
    auto still_running = fetch_status(server, id, 5000);
    expect(still_running.find("\"state\":\"running\"") != std::string::npos,
           "solve still running right after the write (measurement window valid)");

    // 4. solve 最终 completed（有界轮询；实测亚秒~1s，45s 预算宽裕）。
    auto completed = poll_status_until(server, id, "\"state\":\"completed\"", 45000);
    const auto t_done = std::chrono::steady_clock::now();
    expect(completed.find("\"state\":\"completed\"") != std::string::npos, "solve completes end-to-end");
    expect(completed.find("\"result\":{") != std::string::npos, "completed snapshot carries result");
    expect(completed.find("\"success\":true") != std::string::npos, "completed solve result success");

    // 5. durability 端到端（A2 审查补充）：目标装备 max_durability 回填链路
    //    （worker 快照后回填 → apply → recall → format_json）。
    expect(completed.find("\"durability\":528") != std::string::npos,
           "completed result target equipment durability backfilled to 528 (diamond_chestplate)");
    expect(completed.find("diamond_chestplate") != std::string::npos, "completed result target item is diamond_chestplate");

    const int64_t solve_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_done - t_submit).count();
    std::fprintf(stderr, "[P0-A3] read=%lldms write=%lldms solve=%lldms budget=%lldms\n", (long long)read_ms,
                 (long long)write_ms, (long long)solve_ms, (long long)kProfileAccessBudgetMs);

    TEST_PASS("test_solve_does_not_block_profile");
}

} // namespace

// ---------------------------------------------------------------------------
// Suite: build the in-process stack mirroring besq-gui, then drive it with raw
// sockets. server is declared after module so it is destroyed first (WebModule
// dtor ordering invariant).
// ---------------------------------------------------------------------------
static void run_suite() {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    WebModule module(ctx);
    module.set_static_resources({
        {"/index.html", {"text/html", kIndexHtml}},
    });

    HttpServer server;
    server.set_fallback([&](const HttpRequest& r) { return module.dispatch(r); });
    expect(server.start("127.0.0.1", 0), "server starts");
    std::thread server_thread([&] { server.run(); });

    try {
        test_static_and_root(server);
        test_health_status_settings(server);
        test_profiles(server);
        test_enchantables(server);
        test_error_envelopes(server);
        test_task_submit_and_poll(server);
        test_task_failed_snapshot(server);
        test_sse_events(server);
        test_sse_under_close_storm(server);
        test_concurrent_clients(server);
        test_path_traversal(server);
        test_slow_client_timeout(server);
        test_history_endpoints(server);
        test_sse_progress_frames(server);
    } catch (...) {
        // A stray expect failure must not leave the accept-loop thread joinable.
        server.stop();
        server_thread.join();
        throw;
    }

    server.stop();
    server_thread.join();
}

TEST_CASE("test_web_integration") {
    run_suite();
    TEST_PASS("test_web_integration");
}

TEST_CASE_TIMEOUT("test_solve_does_not_block_profile", 120) {
    test_solve_does_not_block_profile();
}
