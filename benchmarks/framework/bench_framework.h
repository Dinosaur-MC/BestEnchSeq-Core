#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// ─── 共享 Benchmark Harness ────────────────────────────────────────────
// 用法：基准文件顶部
//   #define BESQ_BENCH_MAIN
//   #include "framework/bench_framework.h"
// 文件内用 BENCH_CASE("name") { ... } 注册用例（body 为计时区；每次迭代执行
// 一次），不再手写 main()。
//
// 测量方法论（统一）：
//   - warmup 默认 2 次（不计入统计；--warmup N 覆盖）；
//   - iterations：BENCH_CASE_FULL 显式指定，或 0 = 自适应——单次试跑估时后
//     目标总时长 ~1s，clamp [3, 1000]；慢 case（单次 >200ms）直接 1 次且不再
//     warmup（避免重复跑昂贵用例）；
//   - 统计：median / p95 / best（每次迭代耗时排序后取值）+ 总时长 + 迭代数；
//   - 输出：控制台人类可读（`name: median X  p95 Y  best Z  (N iters)`）或
//     --json 机器可解析（单文档含全部 case）。
//
// 参数：--list / --filter <子串>（大小写不敏感）/ --iterations N /
//       --warmup N / --json / --help。--filter 无匹配 → 警告 + exit 1。
// 依赖：零（不链接 besq-core，queue/eloop 等独立基准可用）。

namespace bench {

struct Case {
    std::string name;
    std::function<void()> body;    // 计时区：每次迭代执行一次
    std::function<void()> setup;   // 每次迭代前（不计时）
    std::function<void()> teardown;
    int iterations = 0;            // 0 = 自适应
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*body)(), int iters,
              std::function<void()> setup = {},
              std::function<void()> teardown = {}) {
        registry().push_back(Case{name, body, std::move(setup), std::move(teardown), iters});
    }
};

struct Stats {
    int64_t median_ns = 0;
    int64_t p95_ns = 0;
    int64_t best_ns = 0;
    int64_t total_ns = 0;
    int iterations = 0;
};

/// 自适应迭代数：单次试跑估时 → 目标 ~1s，clamp [3, 1000]；慢 case 直接 1。
inline int auto_iterations(int64_t shot_ns) {
    if (shot_ns > 200'000'000)
        return 1;
    int n = static_cast<int>(1'000'000'000 / std::max<int64_t>(shot_ns, 1));
    return std::clamp(n, 3, 1000);
}

/// 运行一个 case：warmup（不计时）→ 自适应或显式迭代 → 统计。
/// 返回的 Stats 按 iterations 决定；iterations <= 0 时内部自适应。
inline Stats run_case(const Case& c, int iterations, int warmup) {
    Stats s;
    if (iterations <= 0) {
        // 试跑一次决定迭代数；慢 case 跳过 warmup（昂贵用例不再多跑）。
        if (c.setup)
            c.setup();
        auto t0 = std::chrono::steady_clock::now();
        c.body();
        auto t1 = std::chrono::steady_clock::now();
        if (c.teardown)
            c.teardown();
        const int64_t shot =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        iterations = auto_iterations(shot);
        if (shot > 200'000'000)
            warmup = 0;
    }
    for (int i = 0; i < warmup; ++i) {
        if (c.setup)
            c.setup();
        c.body();
        if (c.teardown)
            c.teardown();
    }
    std::vector<int64_t> samples;
    samples.reserve(static_cast<size_t>(iterations));
    for (int i = 0; i < iterations; ++i) {
        if (c.setup)
            c.setup();
        auto t0 = std::chrono::steady_clock::now();
        c.body();
        auto t1 = std::chrono::steady_clock::now();
        if (c.teardown)
            c.teardown();
        const int64_t ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        samples.push_back(ns);
        s.total_ns += ns;
    }
    std::sort(samples.begin(), samples.end());
    s.iterations = iterations;
    s.best_ns = samples.front();
    s.median_ns = samples[samples.size() / 2];
    const size_t p95_idx = static_cast<size_t>(
        std::min<int64_t>(static_cast<int64_t>(samples.size()) - 1,
                          static_cast<int64_t>(samples.size()) * 95 / 100));
    s.p95_ns = samples[p95_idx];
    return s;
}

/// 纳秒 → 人类可读（ns/us/ms/s，自适应小数位）。
inline std::string fmt_dur(int64_t ns) {
    char buf[40];
    if (ns >= 1'000'000'000)
        std::snprintf(buf, sizeof buf, "%.2fs", ns / 1e9);
    else if (ns >= 1'000'000)
        std::snprintf(buf, sizeof buf, "%.1fms", ns / 1e6);
    else if (ns >= 1'000)
        std::snprintf(buf, sizeof buf, "%.1fus", ns / 1e3);
    else
        std::snprintf(buf, sizeof buf, "%lldns", static_cast<long long>(ns));
    return buf;
}

struct Config {
    std::string filter;
    int iterations = 0;  // 0 = per-case 自适应
    int warmup = 2;
    bool json = false;
    bool list_only = false;
};

inline void print_usage(const char* prog) {
    std::cout << "usage: " << prog
              << " [--list] [--filter <substr>] [--iterations N] [--warmup N]"
              << " [--json] [--help]\n";
}

/// 标准参数解析（BESQ_BENCH_MAIN 用）；未知参数 → usage + exit 1。
inline Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (a == "--list")
            cfg.list_only = true;
        else if (a == "--json")
            cfg.json = true;
        else if (a == "--filter") {
            if (i + 1 < argc)
                cfg.filter = argv[++i];
            else {
                print_usage(argv[0]);
                std::exit(1);
            }
        } else if (a == "--iterations") {
            if (i + 1 < argc) {
                cfg.iterations = std::atoi(argv[++i]);
                if (cfg.iterations < 1)
                    cfg.iterations = 1;
            } else {
                print_usage(argv[0]);
                std::exit(1);
            }
        } else if (a == "--warmup") {
            if (i + 1 < argc) {
                cfg.warmup = std::atoi(argv[++i]);
                if (cfg.warmup < 0)
                    cfg.warmup = 0;
            } else {
                print_usage(argv[0]);
                std::exit(1);
            }
        } else {
            print_usage(argv[0]);
            std::exit(1);
        }
    }
    return cfg;
}

namespace detail {
inline std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}
inline bool match(const std::string& name, const std::string& filter) {
    return filter.empty() ||
           lower(name).find(lower(filter)) != std::string::npos;
}
} // namespace detail

/// 执行全部匹配 case 并输出（控制台或 JSON）。
inline int run(const std::vector<Case>& cases, const Config& cfg) {
    if (cfg.list_only) {
        for (const auto& c : cases)
            if (detail::match(c.name, cfg.filter))
                std::cout << c.name << "\n";
        return 0;
    }
    struct Result {
        const Case* c;
        Stats s;
    };
    std::vector<Result> results;
    for (const auto& c : cases) {
        if (!detail::match(c.name, cfg.filter))
            continue;
        const int iters = cfg.iterations > 0 ? cfg.iterations : c.iterations;
        results.push_back({&c, run_case(c, iters, cfg.warmup)});
    }
    if (results.empty() && !cfg.filter.empty()) {
        std::cout << "warning: --filter \"" << cfg.filter
                  << "\" matched no benchmark case" << std::endl;
        return 1;
    }
    if (cfg.json) {
        // 手建 JSON（harness 零依赖，不引入 src 的 Json）。
        std::cout << "{\n  \"cases\": [";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            std::cout << (i ? ",\n    " : "\n    ") << "{\"name\": \"" << r.c->name
                      << "\", \"iterations\": " << r.s.iterations
                      << ", \"median_ns\": " << r.s.median_ns
                      << ", \"p95_ns\": " << r.s.p95_ns
                      << ", \"best_ns\": " << r.s.best_ns
                      << ", \"total_ns\": " << r.s.total_ns << "}";
        }
        std::cout << (results.empty() ? "" : "\n  ") << "]\n}\n";
    } else {
        for (const auto& r : results)
            std::cout << r.c->name << ": median " << fmt_dur(r.s.median_ns)
                      << "  p95 " << fmt_dur(r.s.p95_ns) << "  best "
                      << fmt_dur(r.s.best_ns) << "  (" << r.s.iterations
                      << " iters)" << std::endl;
    }
    return 0;
}

} // namespace bench

#define BESQ_CAT2(a, b) a##b
#define BESQ_CAT(a, b) BESQ_CAT2(a, b)

/// 注册：仅 body（iterations = 0 = 自适应）。
#define BENCH_CASE(name) \
    static void BESQ_CAT(bench_case_, __LINE__)(); \
    static const ::bench::Registrar BESQ_CAT(bench_reg_, __LINE__)(name, &BESQ_CAT(bench_case_, __LINE__), 0); \
    static void BESQ_CAT(bench_case_, __LINE__)()

/// 注册：显式迭代数 + 每迭代 setup/teardown（不计时；lambda 于静态初始化时捕获）：
///   BENCH_CASE_FULL("x", 100, [] { prepare(); }, [] { cleanup(); })
#define BENCH_CASE_FULL(name, iters, setup_fn, teardown_fn) \
    static void BESQ_CAT(bench_case_, __LINE__)(); \
    static const ::bench::Registrar BESQ_CAT(bench_reg_, __LINE__)(name, &BESQ_CAT(bench_case_, __LINE__), (iters), (setup_fn), (teardown_fn)); \
    static void BESQ_CAT(bench_case_, __LINE__)()

#ifdef BESQ_BENCH_MAIN
int main(int argc, char** argv) {
    const auto cfg = ::bench::parse_args(argc, argv);
    return ::bench::run(::bench::registry(), cfg);
}
#endif
