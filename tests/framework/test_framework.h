#pragma once

#include "test_utils.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// ─── TEST_CASE 自动注册 + 共享 main() ─────────────────────────────────
// 用法：测试文件顶部
//   #define BESQ_TEST_MAIN
//   #include "framework/test_framework.h"
// 文件内用 TEST_CASE("name") { ... } 注册用例，不再手写 main()。
// 每个用例独立异常兜底：断言失败（test_error）→ FAIL；被测代码抛异常 →
// UNEXPECTED；SKIP("reason") → 跳过计数（不计失败）。退出码 0=全过 1=有失败。
// 参数：--list / --filter <子串>（大小写不敏感）/ --repeat N / --verbose / --help。
// 契约：test_error 仅应由 expect 系列断言抛出——它们失败时已计入 tests_failed
// 并抛 test_error，共享 main 的 test_error 分支只打印不重复计数；测试代码不得
// 直接抛 test_error（会漏计数）。非 test_error 异常由 main 计数。
// skip_error 派生自 test_error：expect_throws 的 catch (test_error&) 转发分支会让
// SKIP 穿透断言辅助函数继续向上传播（SKIP 在 expect 条件内的语义同 case 级）。
// 注意：吞断言型 runner（如 RUN_TEST 的 catch(test_error){print} 不 rethrow）会
// 同时吞掉 SKIP——此类宏内部不要使用 SKIP()；共享 main 的计数增量检测会把
// 被吞的断言失败如实标为 FAIL（最终审查 P3 记录）。
// 粒度提示：场景合一文件（整 main 合并为单个 TEST_CASE）下 --filter 只能按
// 文件名粒度匹配；--filter 无匹配时打印警告并以 1 退出（防拼写错误假绿灯）。

namespace besq_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> reg;
    return reg;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back(TestCase{name, fn});
    }
};

// SKIP("reason")：case 内抛出，共享 main 捕获并记为跳过（不计失败）。
// 派生自 test_error（而非 std::runtime_error）：expect_throws 等断言辅助的
// catch (const test_error&) { throw; } 转发分支会原样透传，SKIP 不会在断言
// 辅助函数内部被吞掉或误计为 PASS（代码质量审查建议 2）。
struct skip_error : test_error {
    using test_error::test_error;
};

inline int skipped_count = 0;

namespace detail {
inline std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
} // namespace detail

inline void print_usage(const char* prog) {
    std::cout << "usage: " << prog
              << " [--list] [--filter <substr>] [--repeat N] [--verbose] [--help]\n";
}

inline int run_tests(int argc, char** argv) {
    std::string filter;
    bool list_only = false;
    int repeat = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { print_usage(argv[0]); return 0; }
        else if (a == "--list") list_only = true;
        else if (a == "--verbose") test_verbose = true;
        else if (a == "--filter") {
            if (i + 1 < argc) filter = argv[++i];
            else { print_usage(argv[0]); return 1; }
        } else if (a == "--repeat") {
            if (i + 1 < argc) { repeat = std::atoi(argv[++i]); if (repeat < 1) repeat = 1; }
            else { print_usage(argv[0]); return 1; }
        } else { print_usage(argv[0]); return 1; }
    }

    if (list_only) {
        // --list 与 --filter 可组合：只列出名字含子串的 case（代码质量审查建议 3）。
        for (const auto& c : registry())
            if (filter.empty() ||
                detail::lower(c.name).find(detail::lower(filter)) != std::string::npos)
                std::cout << c.name << "\n";
        return 0;
    }

    const std::string f = detail::lower(filter);
    size_t matched = 0;
    for (const auto& c : registry()) {
        if (!filter.empty() && detail::lower(c.name).find(f) == std::string::npos)
            continue;
        ++matched;
        for (int r = 0; r < repeat; ++r) {
            // 计数增量检测：case 正常返回但 tests_failed 有增量 = 内部吞掉了断言
            // 失败（RUN_TEST 模式，expect 已计数）→ case 行如实标 FAIL，不误报
            // PASS，且不重复计数（质量审查 Important #1）。
            const int failed_before = tests_failed;
            try {
                c.fn();
                if (tests_failed > failed_before) {
                    std::cout << "FAIL: " << c.name << ": "
                              << (tests_failed - failed_before)
                              << " assertion(s) failed inside" << std::endl;
                } else {
                    std::cout << "PASS: " << c.name << std::endl;
                }
            } catch (const skip_error& e) {
                ++skipped_count;
                std::cout << "SKIP: " << c.name << ": " << e.what() << std::endl;
            } catch (const test_error& e) {
                // expect 系列失败已计入 tests_failed；这里只打印，避免重复计数。
                std::cout << "FAIL: " << c.name << ": " << e.what() << std::endl;
            } catch (const std::exception& e) {
                ++tests_failed;
                std::cout << "UNEXPECTED: " << c.name << ": " << e.what() << std::endl;
            } catch (...) {
                ++tests_failed;
                std::cout << "UNEXPECTED: " << c.name << ": unknown exception" << std::endl;
            }
        }
    }
    const int rc = print_summary();
    if (skipped_count > 0)
        std::cout << "Skipped: " << skipped_count << std::endl;
    if (!filter.empty() && matched == 0) {
        // 质量审查 Important #1：--filter 无匹配 = 拼写错误或粒度不匹配，
        // 静默 0 断言 + exit 0 会呈现假绿灯——警告并以 1 退出。
        std::cout << "warning: --filter \"" << filter << "\" matched no test case"
                  << std::endl;
        return 1;
    }
    return rc;
}

} // namespace besq_test

#define BESQ_CAT2(a, b) a##b
#define BESQ_CAT(a, b) BESQ_CAT2(a, b)

#define TEST_CASE(name) \
    static void BESQ_CAT(besq_case_, __LINE__)(); \
    static const ::besq_test::Registrar BESQ_CAT(besq_reg_, __LINE__)(name, &BESQ_CAT(besq_case_, __LINE__)); \
    static void BESQ_CAT(besq_case_, __LINE__)()

#define SKIP(reason) throw ::besq_test::skip_error(reason)

#ifdef BESQ_TEST_MAIN
int main(int argc, char** argv) {
    return besq_test::run_tests(argc, argv);
}
#endif
