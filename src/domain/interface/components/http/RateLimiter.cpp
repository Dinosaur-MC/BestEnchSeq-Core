#include "RateLimiter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace web {

namespace {

/// FNV-1a 64 位；0 保留给空槽标记（哈希恰为 0 时映射为 1）。
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

constexpr uint64_t kScale = 65536;           // 定点比例 ×2^16
constexpr uint64_t kMaxScaled = 0xFFFFFFFFu; // 高 32 位满值

} // namespace

RateLimiter::RateLimiter(RateLimitConfig cfg) : _table(new Slot[cfg.slots > 0 ? cfg.slots : 1]), _cfg(std::move(cfg)) {}

bool RateLimiter::take(std::atomic<uint64_t>& st, double rate, double cap) {
    using namespace std::chrono;
    // 低 32 位毫秒时间戳：无符号回绕算术正确（49.7 天周期；闲置超周期时
    // refill 被 cap 钳制，无害）。
    const uint64_t now_ms =
        static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()) & 0xFFFFFFFFu;
    const uint64_t cap_scaled = std::min<uint64_t>(cap, kMaxScaled / kScale) * kScale;
    const double rate_scaled = rate * (static_cast<double>(kScale) / 1000.0); // 每毫秒定点令牌
    uint64_t word = st.load(std::memory_order_relaxed);
    for (;;) {
        const uint64_t tokens = word >> 32;
        const uint64_t last = word & 0xFFFFFFFFu;
        uint64_t next_tokens = tokens;
        if (last == 0) {
            next_tokens = cap_scaled; // 首次使用：满桶
        } else {
            const uint64_t elapsed = (now_ms - last) & 0xFFFFFFFFu; // 回绕安全
            // 饱和加法防溢出（cap 封顶后再封一次）
            if (elapsed >= (cap_scaled - tokens) / rate_scaled)
                next_tokens = cap_scaled;
            else
                next_tokens = tokens + static_cast<uint64_t>(elapsed * rate_scaled);
        }
        if (next_tokens >= kScale) {
            const uint64_t next_word = ((next_tokens - kScale) << 32) | now_ms;
            if (st.compare_exchange_weak(word, next_word, std::memory_order_relaxed))
                return true;
            continue; // word 已被刷新，重算
        }
        // 无令牌：尽力推进时间（下个请求从 now 起算 refill）
        st.compare_exchange_weak(word, (next_tokens << 32) | now_ms, std::memory_order_relaxed);
        return false;
    }
}

HttpResponse RateLimiter::denied(double rate) {
    HttpResponse r = HttpResponse::error(429, "RATE_LIMITED", "rate limit exceeded");
    double wait_s = 0;
    if (rate > 0)
        wait_s = std::ceil(1.0 / rate); // 保守：至少 1 枚令牌的等待
    r.headers.emplace_back("Retry-After", std::to_string(static_cast<long>(wait_s)));
    return r;
}

RateLimiter::Slot* RateLimiter::locate(const std::string& ip) {
    const uint64_t h = fnv1a(ip);
    const size_t n = _cfg.slots;
    for (size_t i = 0; i < n; ++i) {
        Slot* slot = &_table[(h + i) % n];
        uint64_t cur = slot->hash.load(std::memory_order_relaxed);
        if (cur == h)
            return slot;
        if (cur == 0) {
            if (slot->hash.compare_exchange_weak(cur, h, std::memory_order_relaxed))
                return slot; // 认领空槽
            continue;        // 他者先占，继续探测
        }
    }
    // 表满：替换第一个异主槽（仅当活跃 IP 数 > slots 时发生；被换 IP 下次
    // 请求重新认领——有限规模的必然代价，新桶满桶放行一次）。
    for (size_t i = 0; i < n; ++i) {
        Slot* slot = &_table[(h + i) % n];
        uint64_t cur = slot->hash.load(std::memory_order_relaxed);
        if (cur != h && cur != 0) {
            if (slot->hash.compare_exchange_weak(cur, h, std::memory_order_relaxed)) {
                slot->state.store(0, std::memory_order_relaxed);
                return slot;
            }
        }
    }
    return &_table[h % n]; // 极端竞争兜底
}

HttpResponse RateLimiter::operator()(const HttpRequest& req, const Next& next) {
    if (!_cfg.enabled)
        return next(req);
    const std::string ip = client_addr(req, _cfg.client_addr_policy);
    if (!ip.empty()) {
        Slot* slot = locate(ip);
        if (!take(slot->state, _cfg.ip_rps, static_cast<double>(_cfg.ip_burst)))
            return denied(_cfg.ip_rps);
    }
    if (!take(_global, _cfg.global_rps, static_cast<double>(_cfg.global_burst)))
        return denied(_cfg.global_rps);
    return next(req);
}

Middleware make_rate_limiter(RateLimitConfig cfg) {
    return Middleware{[r = std::make_shared<RateLimiter>(std::move(cfg))](const HttpRequest& req, const Next& next) {
        return (*r)(req, next);
    }};
}

} // namespace web
