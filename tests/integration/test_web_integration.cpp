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
//
// All waits are bounded loops; nothing can hang the suite indefinitely.
// =============================================================================

#include "common/log/Logger.h"
#include "common/log/LogRingBuffer.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Socket.h"
#include "domain/interface/web/WebModule.h"
#define BESQ_TEST_MAIN

#include "framework/test_framework.h"
#include <atomic>
#include <cstdio>
#include <chrono>
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
// 修复：快照 fd 集合持锁构建 → 无锁 select → 重取锁按身份校验投递；socket
// 关闭延迟到 poller 线程（close_queue drain），快照 fd 永不提前关闭。
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
    expect(got.find("event: completed") != std::string::npos,
           "SSE completed frame delivered during close-storm");
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
// Case 11 (I-1): /api/logs/events live tail over the real SSE wire.
//
// Requires a LogRingBuffer installed BEFORE WebModule construction (run_suite
// does that) so WebModule registers its one-time listener. A log() call then
// fans out: ring push → listener → hub "logs" key → SSE subscriber connection.
// The client must receive a `data: {"logs":[...]}` frame carrying the marker.
// ---------------------------------------------------------------------------
void test_logs_sse_live(HttpServer& server) {
    int c = sock_connect("127.0.0.1", server.port());
    expect(c >= 0, "logs SSE client connects");
    sock_send(c, "GET /api/logs/events HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    set_nonblocking(c);

    std::string got;
    recv_until(c, got, "text/event-stream", 200); // 流头先就绪
    expect(got.find("HTTP/1.1 200 OK") != std::string::npos, "logs SSE stream head 200");
    expect(got.find("text/event-stream") != std::string::npos, "logs SSE content-type");

    // 订阅建立后再写日志 → 监听器 → hub → SSE 连接。
    Logger::instance().info("besq-live-marker-42");
    const std::string marker = "besq-live-marker-42";
    recv_until(c, got, marker.c_str(), 400); // ≤4s
    const auto pos = got.find(marker);
    expect(pos != std::string::npos, "live log frame reaches the SSE client");
    if (pos != std::string::npos) {
        // 帧字段在 marker 之前（seq/level 先于 message）→ 用 rfind 从 marker 向前找。
        expect(got.rfind("data: ", pos) != std::string::npos, "live frame is an SSE data frame");
        expect(got.rfind("\"logs\"", pos) != std::string::npos, "live frame carries the {logs:[...]} envelope");
        expect(got.rfind("\"level\"", pos) != std::string::npos, "live frame record carries level");
    }
    sock_close(c);
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

    // I-1：/api/logs/events 实时尾。ring 必须在 WebModule 构造前安装——构造期注册
    // 一次监听器（Logger singleton 持有 ring，生命周期覆盖整个 suite）。
    auto ring = std::make_shared<LogRingBuffer>(1024);
    Logger::instance().set_ring_buffer(ring);

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
        test_logs_sse_live(server);
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
