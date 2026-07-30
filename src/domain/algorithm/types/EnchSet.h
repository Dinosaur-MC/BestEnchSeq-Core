#pragma once
#include "Enchantment.h"
#include <bit>
#include <span>
#include <type_traits>

namespace algorithm {

class EnchSet {
  public:
    using mask_type                   = uint64_t;
    using value_type                  = Ench::value_type; // uint8_t
    constexpr static size_t MAX_SIZE = sizeof(mask_type) * 8;
    constexpr static uint8_t npos = static_cast<uint8_t>(MAX_SIZE); // End of range

    // 只读代理
    class ConstEnchRef {
      public:
        constexpr ConstEnchRef(const EnchSet &set, EnchSet::value_type idx) noexcept : set_(set), idx_(idx) {}

        // 隐式转换为 Ench（读）
        operator Ench() const noexcept { return Ench{idx_, set_._lvls[idx_]}; }
        // 允许使用 `->` 操作符访问成员
        const ConstEnchRef *operator->() const noexcept { return this; }

        [[nodiscard]] value_type id() const noexcept { return idx_; }
        [[nodiscard]] value_type level() const noexcept { return set_._lvls[idx_]; }

        friend bool operator==(ConstEnchRef a, ConstEnchRef b) noexcept {
            return a.id() == b.id() && a.level() == b.level();
        }
        friend bool operator==(ConstEnchRef a, const Ench &b) noexcept {
            return a.id() == b.id && a.level() == b.level;
        }
        friend bool operator==(const Ench &a, ConstEnchRef b) noexcept { return b == a; }

      private:
        const EnchSet &set_;
        value_type idx_;
    };

    // 可写代理（仅 level 可写）
    class EnchRef {
      public:
        constexpr EnchRef(EnchSet &set, value_type idx) noexcept : set_(set), idx_(idx) {}

        operator Ench() const noexcept { return Ench{idx_, set_._lvls[idx_]}; }
        // 允许使用 `->` 操作符访问成员
        const EnchRef *operator->() const noexcept { return this; }
        EnchRef *operator->() noexcept { return this; }

        /// Only modify `Ench::level`, `Ench::id` is ignored
        EnchRef &operator=(const Ench &e) noexcept {
            set_._lvls[idx_] = e.level;
            set_._hash_cache = 0;
            return *this;
        }
        /// Only modify `Ench::level`, `Ench::id` is ignored
        EnchRef &operator=(const ConstEnchRef &o) noexcept {
            set_._lvls[idx_] = o.level();
            set_._hash_cache = 0;
            return *this;
        }

        [[nodiscard]] value_type id() const noexcept { return idx_; }
        [[nodiscard]] value_type level() const noexcept { return set_._lvls[idx_]; }
        [[nodiscard]] value_type &level() noexcept {
            set_._hash_cache = 0;
            return set_._lvls[idx_];
        }

        friend bool operator==(EnchRef a, EnchRef b) noexcept {
            return a.id() == b.id() && a.level() == b.level();
        }
        friend bool operator==(EnchRef a, const Ench &b) noexcept {
            return a.id() == b.id && a.level() == b.level;
        }
        friend bool operator==(const Ench &a, EnchRef b) noexcept { return b == a; }

      private:
        EnchSet &set_;
        value_type idx_;
    };

  private:
    // 迭代器
    template <typename SetType> class EnchSetIterator {
      public:
        using value_type        = Ench;
        using difference_type   = std::ptrdiff_t;
        using reference         = std::conditional_t<std::is_const_v<SetType>, ConstEnchRef, EnchRef>;
        using pointer           = reference;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept  = std::input_iterator_tag;

        constexpr EnchSetIterator() noexcept : set_(nullptr), idx_(npos) {}
        constexpr EnchSetIterator(SetType *set, EnchSet::value_type idx) noexcept : set_(set), idx_(idx) {}

        constexpr reference operator*() const noexcept { return reference(*set_, idx_); }
        constexpr pointer operator->() const noexcept { return reference(*set_, idx_); }

        constexpr EnchSetIterator &operator++() noexcept {
            if (idx_ < npos) {
                uint64_t mask = set_->_mask >> (idx_ + 1);
                idx_          = mask == 0 ? npos : idx_ + 1 + std::countr_zero(mask);
            }
            return *this;
        }

        constexpr EnchSetIterator operator++(int) noexcept {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const EnchSetIterator &rhs) const noexcept {
            return idx_ == rhs.idx_ && set_ == rhs.set_;
        }

      private:
        SetType *set_;
        EnchSet::value_type idx_; // EnchSet::npos 表示结束
    };

  public:
    using iterator       = EnchSetIterator<EnchSet>;
    using const_iterator = EnchSetIterator<const EnchSet>;

    EnchSet() noexcept = default;
    EnchSet(std::initializer_list<Ench> il) noexcept;
    template <
        typename Iter,
        std::enable_if_t<std::is_convertible_v<decltype(*std::declval<Iter &>()), Ench>, int> = 0>
    EnchSet(Iter first, Iter last) noexcept {
        for (auto it = first; it != last; ++it)
            insert(*it);
    }

    ~EnchSet() noexcept = default;

    // ── Iterators ──
    iterator begin() noexcept;
    iterator end() noexcept;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;

    // ── Capacity ──
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    // ── Lookup ──
    [[nodiscard]] iterator find(const Ench &ench) noexcept;
    [[nodiscard]] const_iterator find(const Ench &ench) const noexcept;
    [[nodiscard]] iterator find(const EnchRef &ench) noexcept;
    [[nodiscard]] const_iterator find(const ConstEnchRef &ench) const noexcept;

    [[nodiscard]] mask_type get_mask() const noexcept { return _mask; }
    [[nodiscard]] value_type at(value_type id) const;
    [[nodiscard]] value_type operator[](value_type id) noexcept;
    [[nodiscard]] value_type operator[](value_type id) const noexcept;
    [[nodiscard]] bool contains(value_type id) const noexcept;
    /// Returns the first valid index, or `EnchSet::npos` if no valid index exists
    [[nodiscard]] value_type first() const noexcept;
    /// Returns the next valid index after `id`, or `EnchSet::npos` if no valid index exists
    [[nodiscard]] value_type next(value_type id) const noexcept;
    /// Returns the next valid level after `id`, or `0` if no valid level exists
    [[nodiscard]] value_type next_level(value_type id) const noexcept;
    [[nodiscard]] std::span<const value_type, MAX_SIZE> data() const noexcept;

    // ── Modifiers ──
    bool insert(const Ench &ench) noexcept;
    bool insert(value_type id, value_type level) noexcept;
    bool erase(value_type id) noexcept;
    bool erase(iterator pos) noexcept;
    void clear() noexcept;

    // ── Hash (lazily cached) ──
    [[nodiscard]] size_t hash() const noexcept;
    void rehash() const noexcept;

    // ── Comparison ──
    bool operator==(const EnchSet &o) const noexcept;

    // --- Utils ---
    [[nodiscard]] mask_type operator&(const EnchSet &o) const noexcept { return _mask & o._mask; }
    [[nodiscard]] mask_type operator|(const EnchSet &o) const noexcept { return _mask | o._mask; }
    [[nodiscard]] mask_type operator-(const EnchSet &o) const noexcept { return _mask & ~o._mask; }
    [[nodiscard]] mask_type operator&(mask_type o) const noexcept { return _mask & o; }
    [[nodiscard]] mask_type operator|(mask_type o) const noexcept { return _mask | o; }
    [[nodiscard]] mask_type operator-(mask_type o) const noexcept { return _mask & ~o; }

    // ── Serialization (non-virtual; value-type) ──
    void serialize(ByteStreamWriter &w) const noexcept;
    void deserialize(ByteStreamReader &r) noexcept;

  private:
    alignas(value_type) value_type _lvls[MAX_SIZE]{};
    size_t _size{0};
    mask_type _mask{0};
    mutable size_t _hash_cache{0};
};
static_assert(std::input_iterator<EnchSet::iterator>);
static_assert(std::input_iterator<EnchSet::const_iterator>);
static_assert(std::is_standard_layout_v<EnchSet>);
static_assert(std::has_unique_object_representations_v<EnchSet>);
static_assert(std::is_trivially_copyable_v<EnchSet>);
static_assert(std::is_trivially_move_assignable_v<EnchSet>);

// ── Free-function streaming for EnchSet (ADL via algorithm namespace) ──
inline ByteStreamWriter &operator<<(ByteStreamWriter &w, const EnchSet &s) {
    s.serialize(w);
    return w;
}
inline ByteStreamReader &operator>>(ByteStreamReader &r, EnchSet &s) {
    s.deserialize(r);
    return r;
}

} // namespace algorithm

template <> struct std::hash<algorithm::EnchSet> {
    size_t operator()(const algorithm::EnchSet &s) const noexcept { return s.hash(); }
};
