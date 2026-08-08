#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// ─── 共享 Benchmark Harness v2 ─────────────────────────────────────────
// 用法：基准文件顶部
//   #define BESQ_BENCH_MAIN
//   #include "framework/bench_framework.h"
// 文件内用 BENCH_CASE / BENCH_CASE_GROUP / BENCH_CASE_COMPARE 注册用例
// （body 为计时区；每次迭代执行一次），不再手写 main()。
//
// 测量方法论（统一，输出自包含）：
//   - warmup 默认 2 次（不计入统计；--warmup N 覆盖）；迭代数 BENCH_CASE*
//     显式指定或 0 = 自适应（单次试跑估时 → 目标 ~1s，clamp [3, 1000]；
//     慢 case 单次 >200ms 直接 1 次且 warmup=0）；
//   - 统计：median / p95 / best + 迭代数 + 实际 warmup 数（全部标注）；
//   - 吞吐量 ops/s = ops_per_iter × 1e9 / median_ns（ops_per_iter 由注册
//     声明，派生可验证）；speedup = baseline.median / this.median
//     （baseline 须在同组内、先于本 case 注册）；
//   - 输出：控制台按组渲染二维表（组完成即打印，渐进；列自适应）；
//     --json 单文档（含全部标注字段）。
//
// 参数：--list / --filter <子串> / --iterations N / --warmup N / --json / --help。
// 依赖：零（不链接 besq-core，queue/eloop 等独立基准可用）。

namespace bench {

struct Case {
    std::string name;
    std::function<void()> body;    // 计时区：每次迭代执行一次
    std::function<void()> setup;   // 每次迭代前（不计时）
    std::function<void()> teardown;
    int iterations = 0;            // 0 = 自适应
    std::string group;             // 分组（表格章节标题；"" = 独立组）
    int64_t ops_per_iter = 0;      // 每迭代操作数 → 吞吐量 ops/s（0 = 不显示）
    std::string baseline;          // 对比基准 case 名（同组、先注册；"" = 无）
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*body)(), int iters,
              std::string group = {}, int64_t ops_per_iter = 0,
              std::string baseline = {}, std::function<void()> setup = {},
              std::function<void()> teardown = {}) {
        registry().push_back(
            Case{name, body, std::move(setup), std::move(teardown), iters,
                 std::move(group), ops_per_iter, std::move(baseline)});
    }
};

struct Stats {
    int64_t median_ns = 0;
    int64_t p95_ns = 0;
    int64_t best_ns = 0;
    int64_t total_ns = 0;
    int iterations = 0;
    int warmup = 0;  // 实际使用的 warmup 次数（标注输出）
};

/// 自适应迭代数：单次试跑估时 → 目标 ~1s，clamp [3, 1000]；慢 case 直接 1。
inline int auto_iterations(int64_t shot_ns) {
    if (shot_ns > 200'000'000)
        return 1;
    int n = static_cast<int>(1'000'000'000 / std::max<int64_t>(shot_ns, 1));
    return std::clamp(n, 3, 1000);
}

/// 运行一个 case：warmup（不计时）→ 自适应或显式迭代 → 统计。
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
    s.warmup = warmup;
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

/// 每秒操作数 → 人类可读（K/M/G）。
inline std::string fmt_ops(int64_t ops_per_sec) {
    char buf[32];
    if (ops_per_sec >= 1'000'000'000)
        std::snprintf(buf, sizeof buf, "%.2fG", ops_per_sec / 1e9);
    else if (ops_per_sec >= 1'000'000)
        std::snprintf(buf, sizeof buf, "%.1fM", ops_per_sec / 1e6);
    else if (ops_per_sec >= 1'000)
        std::snprintf(buf, sizeof buf, "%.0fK", ops_per_sec / 1e3);
    else
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(ops_per_sec));
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
inline std::string pad(const std::string& s, size_t w) {
    std::string out = s;
    if (out.size() < w)
        out.append(w - out.size(), ' ');
    return out;
}
inline std::string pad_left(const std::string& s, size_t w) {
    std::string out;
    if (s.size() < w)
        out.append(w - s.size(), ' ');
    out += s;
    return out;
}
} // namespace detail

struct Result {
    const Case* c;
    Stats s;
    int64_t ops_per_sec = 0;  // 派生：ops_per_iter × 1e9 / median_ns
    double speedup = 0.0;     // 派生：同组 baseline 的 median / 本 median
    int64_t group_ns = 0;     // 组内 wall 时长（section 行）
};

/// 渲染一个组的二维表（动态列宽；可选列按组内数据自适应）。
inline void render_table(const std::string& group, const std::vector<Result>& rows) {
    bool has_ops = false, has_base = false;
    for (const auto& r : rows) {
        if (r.c->ops_per_iter > 0)
            has_ops = true;
        if (!r.c->baseline.empty())
            has_base = true;
    }
    // 列宽：case | median | p95 | best | iters | warmup [| ops/s] [| vs base]
    std::vector<std::string> headers = {"case", "median", "p95", "best", "iters", "warmup"};
    std::vector<size_t> widths = {6, 6, 3, 4, 5, 6};
    std::vector<std::vector<std::string>> cells;
    std::string base_name;
    for (const auto& r : rows)
        if (!r.c->baseline.empty()) {
            base_name = r.c->baseline;
            break;
        }
    if (has_ops) {
        headers.push_back("ops/s");
        widths.push_back(5);
    }
    if (has_base) {
        headers.push_back("vs " + base_name);
        widths.push_back(0);
    }
    for (const auto& r : rows) {
        std::vector<std::string> row = {
            r.c->name,
            fmt_dur(r.s.median_ns),
            fmt_dur(r.s.p95_ns),
            fmt_dur(r.s.best_ns),
            std::to_string(r.s.iterations),
            std::to_string(r.s.warmup),
        };
        if (has_ops)
            row.push_back(fmt_ops(r.ops_per_sec));
        if (has_base) {
            if (r.c->baseline.empty()) {
                row.push_back("—");  // 基准行自身无 speedup
            } else {
                char buf[16];
                std::snprintf(buf, sizeof buf, "%.2fx", r.speedup);
                row.push_back(buf);
            }
        }
        cells.push_back(std::move(row));
    }
    for (size_t i = 0; i < headers.size(); ++i) {
        widths[i] = std::max(widths[i], headers[i].size());
        for (const auto& row : cells)
            widths[i] = std::max(widths[i], row[i].size());
    }
    const auto hline = [&](const char* l, const char* m, const char* r) {
        std::cout << l;
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i)
                std::cout << m;
            for (size_t k = 0; k < widths[i] + 2; ++k)
                std::cout << "─";
        }
        std::cout << r << "\n";
    };
    if (!group.empty())
        std::cout << "── " << group << " ──\n";
    hline("┌", "┬", "┐");
    std::cout << "│";
    for (size_t i = 0; i < headers.size(); ++i)
        std::cout << " " << detail::pad(headers[i], widths[i]) << " │";
    std::cout << "\n";
    hline("├", "┼", "┤");
    for (const auto& row : cells) {
        std::cout << "│";
        for (size_t i = 0; i < headers.size(); ++i) {
            const bool numeric = i >= 1;  // case 列左对齐，其余右对齐
            const std::string cell = numeric ? detail::pad_left(row[i], widths[i])
                                             : detail::pad(row[i], widths[i]);
            std::cout << " " << cell << " │";
        }
        std::cout << "\n";
    }
    hline("└", "┴", "┘");
    const int64_t group_ms = rows.front().group_ns / 1'000'000;
    std::cout << "  section: " << group_ms << "ms (" << rows.size()
              << " cases)" << std::endl;
}

/// 执行全部匹配 case：按组渐进渲染二维表（组完成即打印），末尾 summary。
inline int run(const std::vector<Case>& cases, const Config& cfg) {
    if (cfg.list_only) {
        for (const auto& c : cases)
            if (detail::match(c.name, cfg.filter))
                std::cout << c.name << "\n";
        return 0;
    }
    std::vector<Result> all;
    std::vector<Result> group_rows;
    std::string cur_group;
    auto group_start = std::chrono::steady_clock::now();
    int64_t total_ns = 0;
    int64_t total_iters = 0, total_warmup = 0;
    const auto flush_group = [&]() {
        if (group_rows.empty())
            return;
        // 组内 speedup：baseline 须同组且先注册（注册序 = 执行序）。
        for (auto& r : group_rows) {
            if (r.c->baseline.empty())
                continue;
            for (const auto& b : group_rows)
                if (b.c->name == r.c->baseline && b.s.median_ns > 0) {
                    r.speedup = static_cast<double>(b.s.median_ns) /
                                static_cast<double>(r.s.median_ns);
                    break;
                }
        }
        const int64_t gns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - group_start)
                                .count();
        for (auto& r : group_rows)
            r.group_ns = gns;
        if (!cfg.json)
            render_table(cur_group, group_rows);
        for (const auto& r : group_rows) {
            total_ns += gns;
            total_iters += r.s.iterations;
            total_warmup += r.s.warmup;
        }
        all.insert(all.end(), group_rows.begin(), group_rows.end());
        group_rows.clear();
    };
    for (const auto& c : cases) {
        if (!detail::match(c.name, cfg.filter))
            continue;
        if (c.group != cur_group) {
            flush_group();
            cur_group = c.group;
            group_start = std::chrono::steady_clock::now();
        }
        Result r;
        r.c = &c;
        const int iters = cfg.iterations > 0 ? cfg.iterations : c.iterations;
        r.s = run_case(c, iters, cfg.warmup);
        if (c.ops_per_iter > 0 && r.s.median_ns > 0)
            r.ops_per_sec = static_cast<int64_t>(
                static_cast<double>(c.ops_per_iter) * 1e9 /
                static_cast<double>(r.s.median_ns));
        group_rows.push_back(std::move(r));
    }
    flush_group();
    if (all.empty() && !cfg.filter.empty()) {
        std::cout << "warning: --filter \"" << cfg.filter
                  << "\" matched no benchmark case" << std::endl;
        return 1;
    }
    if (cfg.json) {
        // 手建 JSON（harness 零依赖）；含全部标注字段（自包含）。
        std::cout << "{\n  \"cases\": [";
        for (size_t i = 0; i < all.size(); ++i) {
            const auto& r = all[i];
            std::cout << (i ? ",\n    " : "\n    ") << "{\"name\": \"" << r.c->name
                      << "\", \"group\": \"" << r.c->group
                      << "\", \"iterations\": " << r.s.iterations
                      << ", \"warmup\": " << r.s.warmup
                      << ", \"median_ns\": " << r.s.median_ns
                      << ", \"p95_ns\": " << r.s.p95_ns
                      << ", \"best_ns\": " << r.s.best_ns
                      << ", \"total_ns\": " << r.s.total_ns;
            if (r.c->ops_per_iter > 0)
                std::cout << ", \"ops_per_iter\": " << r.c->ops_per_iter
                          << ", \"ops_per_sec\": " << r.ops_per_sec;
            if (!r.c->baseline.empty()) {
                char buf[24];
                std::snprintf(buf, sizeof buf, "%.6f", r.speedup);
                std::cout << ", \"baseline\": \"" << r.c->baseline
                          << "\", \"speedup\": " << buf;
            }
            std::cout << "}";
        }
        std::cout << (all.empty() ? "" : "\n  ") << "]\n}\n";
    } else {
        // Summary（口径统一：组耗时 = 组 wall 时长 × 组内 case 数不做重复
        // 累计——每组只计一次，与表格 section 行一致）
        std::cout << "\n── Summary ──\n";
        std::map<std::string, size_t> group_cases;
        std::map<std::string, int64_t> group_ms;
        std::map<std::string, int64_t> group_iters;
        for (const auto& r : all) {
            group_cases[r.c->group]++;
            group_iters[r.c->group] += r.s.iterations;
            if (r.group_ns > 0 && group_ms[r.c->group] == 0)
                group_ms[r.c->group] = r.group_ns / 1'000'000;  // 每组计一次
        }
        int64_t sum_ms = 0, sum_iters = 0;
        for (const auto& [g, n] : group_cases) {
            const std::string label = g.empty() ? "(ungrouped)" : g;
            sum_ms += group_ms[g];
            sum_iters += group_iters[g];
            std::cout << "  " << detail::pad(label, 32) << n << " cases, "
                      << group_ms[g] << "ms (" << group_iters[g]
                      << " iters)\n";
        }
        std::cout << "  " << detail::pad("Total", 32) << all.size()
                  << " cases, " << sum_ms << "ms"
                  << " (iterations: " << sum_iters
                  << ", warmup: " << total_warmup << ")" << std::endl;
    }
    return 0;
}

} // namespace bench

#define BESQ_CAT2(a, b) a##b
#define BESQ_CAT(a, b) BESQ_CAT2(a, b)

/// 注册：仅 body（iterations = 0 = 自适应；独立组）。
#define BENCH_CASE(name) \
    static void BESQ_CAT(bench_case_, __LINE__)(); \
    static const ::bench::Registrar BESQ_CAT(bench_reg_, __LINE__)(name, &BESQ_CAT(bench_case_, __LINE__), 0); \
    static void BESQ_CAT(bench_case_, __LINE__)()

/// 注册：分组 + 每迭代操作数（吞吐量列）。
///   BENCH_CASE_GROUP("x", "Sequential throughput", 1000)
#define BENCH_CASE_GROUP(name, group, ops) \
    static void BESQ_CAT(bench_case_, __LINE__)(); \
    static const ::bench::Registrar BESQ_CAT(bench_reg_, __LINE__)(name, &BESQ_CAT(bench_case_, __LINE__), 0, (group), (ops)); \
    static void BESQ_CAT(bench_case_, __LINE__)()

/// 注册：分组 + 操作数 + 对比基准（speedup 列；baseline 须同组且先注册）。
///   BENCH_CASE_COMPARE("y", "throughput", 1000, "x")
#define BENCH_CASE_COMPARE(name, group, ops, baseline) \
    static void BESQ_CAT(bench_case_, __LINE__)(); \
    static const ::bench::Registrar BESQ_CAT(bench_reg_, __LINE__)(name, &BESQ_CAT(bench_case_, __LINE__), 0, (group), (ops), (baseline)); \
    static void BESQ_CAT(bench_case_, __LINE__)()

/// 注册：显式迭代数 + 每迭代 setup/teardown（不计时；lambda 于静态初始化时捕获）：
///   BENCH_CASE_FULL("x", 100, [] { prepare(); }, [] { cleanup(); })
#define BENCH_CASE_FULL(name, iters, setup_fn, teardown_fn) \
    static void BESQ_CAT(bench_case_, __LINE__)(); \
    static const ::bench::Registrar BESQ_CAT(bench_reg_, __LINE__)(name, &BESQ_CAT(bench_case_, __LINE__), (iters), "", 0, "", (setup_fn), (teardown_fn)); \
    static void BESQ_CAT(bench_case_, __LINE__)()

#ifdef BESQ_BENCH_MAIN
int main(int argc, char** argv) {
    const auto cfg = ::bench::parse_args(argc, argv);
    return ::bench::run(::bench::registry(), cfg);
}
#endif
