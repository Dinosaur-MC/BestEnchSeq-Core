#pragma once
#include "HttpCommon.h"
#include "Middleware.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace web {

struct RateLimitConfig {
    bool enabled = false;                       // 默认关闭：现有行为零变化，装配点显式开启
    double ip_rps = 20;                         // 每 IP 补令牌速率（次/秒）
    size_t ip_burst = 40;                       // 每 IP 桶容量（允许突发）
    double global_rps = 200;                    // 全局共享桶速率
    size_t global_burst = 400;                  // 全局桶容量
    size_t slots = 16384;                       // 每 IP 桶表容量（内存有界，固定分配）
    ClientAddrPolicy client_addr_policy;        // trust_forwarded / trusted_proxies
};

/// 每 IP + 全局令牌桶限流中间件（无锁：固定容量 lock-free 哈希表；桶状态
/// 打包进单个 uint64（高 32 位定点令牌 ×2^16，低 32 位最近补充 ms 时间戳），
/// 单 CAS refill+consume+时间推进）。超限 → 429 + Retry-After + 信封
/// {"ok":false,"error":{"code":"RATE_LIMITED",...}}。
class RateLimiter {
public:
    explicit RateLimiter(RateLimitConfig cfg);

    HttpResponse operator()(const HttpRequest& req, const Next& next);

private:
    struct Slot {
        std::atomic<uint64_t> hash{0};      // 0 = 空槽；否则 IP 的 FNV-1a 哈希
        std::atomic<uint64_t> state{0};     // 打包桶状态（高 32 位定点令牌 ×2^16，
                                            // 低 32 位最近补充 ms 时间戳；单 CAS 更新）
    };
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "rate limiter requires lock-free 64-bit atomics");

    /// 桶定位：命中 / 认领空槽 / 表满替换异主槽（被换 IP 下次重新认领）。
    /// 替换时新桶从零开始（首次使用满桶）。极端竞争兜底：首槽（共享一桶，
    /// 限流更严，安全方向）。
    Slot* locate(const std::string& ip);
    /// 单桶取令牌：refill + consume + last 推进一次 CAS 原子完成。
    /// false = 桶空（调用方构造 429）。首次使用（last==0）视为满桶。
    static bool take(std::atomic<uint64_t>& st, double rate, double cap);
    /// 429：Retry-After = ceil(1 / rate) 秒（保守：至少 1 枚令牌的等待；rate<=0 → 0）。
    static HttpResponse denied(double rate);

    std::unique_ptr<Slot[]> _table;     // slots 个槽，构造期一次性分配
    std::atomic<uint64_t> _global{0};   // 全局共享桶
    RateLimitConfig _cfg;
};

/// 中间件工厂。返回的 Middleware 持有 shared_ptr<RateLimiter>（RateLimiter
/// 含 unique_ptr 不可拷贝，经 shared_ptr 装箱满足 std::function 可拷贝约束）。
Middleware make_rate_limiter(RateLimitConfig cfg);

} // namespace web
