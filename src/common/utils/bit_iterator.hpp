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
    using unsigned_type     = std::make_unsigned_t<T>;
    static constexpr U npos = std::numeric_limits<U>::max();

    constexpr bit_iterator() noexcept = default;
    explicit constexpr bit_iterator(T value) noexcept : mask_(static_cast<unsigned_type>(value)) {}

    constexpr void reset(T value) noexcept { mask_ = static_cast<unsigned_type>(value); }

    // 返回下一个 1 的位置（从低到高），无剩余则返回 npos
    [[nodiscard]] constexpr U next() noexcept {
        if (mask_ == 0) {
            return npos;
        }
        const U idx = static_cast<U>(std::countr_zero(mask_));
        mask_ &= mask_ - 1; // 清除最低位 1
        return idx;
    }

    // 是否还有剩余的 1 位
    [[nodiscard]] constexpr bool has_next() const noexcept { return mask_ != 0; }
    [[nodiscard]] constexpr operator bool() const noexcept { return has_next(); }

  private:
    unsigned_type mask_{0};
};

template <std::integral T, std::integral U = size_t>
    requires std::is_unsigned_v<U>
class sbit_iterator {
  public:
    using unsigned_type     = std::make_unsigned_t<T>;
    static constexpr U npos = std::numeric_limits<U>::max();

    constexpr sbit_iterator() noexcept = default;
    explicit constexpr sbit_iterator(T value) noexcept : mask_(static_cast<unsigned_type>(value)) {
        (void)next(); // 预取第一个位置
    }

    constexpr U reset(T value) noexcept {
        mask_ = static_cast<unsigned_type>(value);
        cur_  = npos;
        return next();
    }

    constexpr U next() noexcept {
        if (mask_ == 0) {
            cur_ = npos;
            return npos;
        }
        cur_ = static_cast<U>(std::countr_zero(mask_));
        mask_ &= mask_ - 1;
        return cur_;
    }

    [[nodiscard]] constexpr U get() const noexcept { return cur_; }
    [[nodiscard]] constexpr U operator*() const noexcept { return cur_; }
    [[nodiscard]] constexpr operator bool() const noexcept { return cur_ != npos; }

    // 前置 ++
    constexpr U operator++() noexcept { return next(); }
    // 后置 ++
    constexpr U operator++(int) noexcept {
        U ret = cur_;
        (void)next();
        return ret;
    }

    struct Sentinel {};
    constexpr sbit_iterator &begin() noexcept { return *this; }
    constexpr const sbit_iterator &begin() const noexcept { return *this; }
    constexpr static Sentinel end() noexcept { return {}; }
    constexpr bool operator!=(const Sentinel &) const noexcept { return cur_ != npos; }

  private:
    unsigned_type mask_{0};
    U cur_{npos};
};
