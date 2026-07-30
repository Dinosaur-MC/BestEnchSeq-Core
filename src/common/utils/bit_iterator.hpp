#pragma once
#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

template <std::integral T, std::integral U = size_t>
    requires std::is_unsigned_v<U>
class bit_iterator {
  public:
    using unsigned_type = std::make_unsigned_t<T>;

    // 没有下一个 1 时返回该值
    static constexpr U npos = std::numeric_limits<U>::max();

    // 默认构造：从 0 开始遍历
    constexpr bit_iterator() noexcept = default;
    // 构造：保存无符号位模式，基址从 0 开始
    explicit constexpr bit_iterator(T value) noexcept
        : remaining_(static_cast<unsigned_type>(value)), base_(0) {}

    // 重置为新的整数，重新从最低位开始连续遍历
    constexpr void reset(T value) noexcept {
        remaining_ = static_cast<unsigned_type>(value);
        base_      = 0;
    }

    // 返回下一个 1 的位置（从低位到高位），遍历结束返回 npos
    [[nodiscard]] constexpr U next() noexcept {
        if (remaining_ == 0) {
            return npos;
        }
        // 计算当前剩余部分中最低位 1 的相对偏移
        const U rel = static_cast<U>(std::countr_zero(remaining_));
        // 将当前 1 及其右侧所有 0 移出
        // 注意：当 rel+1 == 位数宽度时（最高位 1），右移整个宽度是 UB，需特殊处理
        constexpr U kBits = static_cast<U>(sizeof(unsigned_type) * 8);
        if (rel + 1 >= kBits) {
            remaining_ = 0;
        } else {
            remaining_ >>= (rel + 1);
        }
        // 更新基址
        base_ += rel;
        return base_++;
    }
    [[nodiscard]] constexpr operator bool() const noexcept { return remaining_ < npos; }

  private:
    unsigned_type remaining_{0};
    U base_{0};
};

template <std::integral T, std::integral U = size_t>
    requires std::is_unsigned_v<U>
class sbit_iterator {
  public:
    using unsigned_type = std::make_unsigned_t<T>;

    // 没有下一个 1 时返回该值
    static constexpr U npos = std::numeric_limits<U>::max();

    // 默认构造：从 0 开始遍历
    constexpr sbit_iterator() noexcept = default;
    // 构造：保存无符号位模式，基址从 0 开始
    explicit constexpr sbit_iterator(T value) noexcept
        : remaining_(static_cast<unsigned_type>(value)), base_(0) {
        (void)next();
    }

    // 重置为新的整数，重新从最低位开始连续遍历
    constexpr U reset(T value) noexcept {
        remaining_ = static_cast<unsigned_type>(value);
        base_      = 0;
        cur_       = npos;
        return next();
    }

    // 返回下一个 1 的位置（从低位到高位），遍历结束返回 npos
    constexpr U next() noexcept {
        if (remaining_ == 0) {
            cur_ = npos;
            return npos;
        }
        // 计算当前剩余部分中最低位 1 的相对偏移
        const U rel = static_cast<U>(std::countr_zero(remaining_));
        cur_        = base_ + rel;
        // 将当前 1 及其右侧所有 0 移出，并更新基址
        constexpr U kBits = static_cast<U>(sizeof(unsigned_type) * 8);
        if (rel + 1 >= kBits) {
            remaining_ = 0;
        } else {
            remaining_ >>= (rel + 1);
        }
        base_ += rel + 1;
        return cur_;
    }

    [[nodiscard]] constexpr U get() const noexcept { return cur_; }
    [[nodiscard]] constexpr U operator*() const noexcept { return cur_; }
    [[nodiscard]] constexpr operator bool() const noexcept { return cur_ < npos; }
    constexpr U operator++() noexcept { return next(); }
    constexpr U operator++(int) noexcept {
        U ret = cur_;
        (void)next();
        return ret;
    }
    struct Sentinel {};
    constexpr sbit_iterator &begin() noexcept { return *this; }
    constexpr const sbit_iterator &begin() const noexcept { return *this; }
    constexpr static Sentinel end() noexcept { return {}; }
    constexpr bool operator!=(const Sentinel &sentinel) noexcept { return cur_ != npos; }

  private:
    unsigned_type remaining_{0};
    U base_{0};
    U cur_{npos};
};
