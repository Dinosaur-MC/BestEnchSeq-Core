#pragma once
#include "CommonTypes.h"
#include <concepts>
#include <cstddef>
#include <functional>
#include <unordered_map>

// ══════════════════════════════════════════════════════════════════════════
// IRegistry<T> — uniform interface for business-domain registries
//
// Backed by std::unordered_map<NSID, T>. All lookup/modify operations are
// O(1) average. No positional index — entries are identified by NSID key.
//
// Subclass only when you need additional side-effects on insert/erase
// (e.g. EnchantmentRegistry's incompatibility table).
//
// Iterators dereference to T& / const T& (value iterators), not pairs,
// so existing range-for loops work unchanged.
// ══════════════════════════════════════════════════════════════════════════

template <typename T>
concept IRegistryEntry = std::copyable<T> &&              // 可复制
                         std::movable<T> &&               // 可移动
                         std::default_initializable<T> && // 零参构造
                         std::equality_comparable<T> &&   // 相等比较（==, !=）
                         std::totally_ordered<T> &&       // 全序比较（<, >, <=, >=）
                         requires(T t) {
                             { std::hash<T>{}(t) } -> std::convertible_to<std::size_t>; // 可哈希
                             { t.id } -> std::convertible_to<NSID>;                     // 有 NSID 成员变量
                         };

template <IRegistryEntry T> class IRegistry {
  public:
    virtual ~IRegistry() = default;

    // -- STL compatible type aliases -------------------------------------------
    using key_type       = NSID;
    using value_type     = T;
    using container_type = std::unordered_map<NSID, T>;

    // -- Value iterator — dereferences to T& (not pair<const NSID, T>&) --------
    template <typename MapIter, typename ValueRef> class value_iterator {
      public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = ptrdiff_t;
        using value_type        = std::remove_cvref_t<ValueRef>;
        using pointer           = std::add_pointer_t<ValueRef>;
        using reference         = ValueRef;

        value_iterator() = default;
        explicit value_iterator(MapIter it) : _it(it) {}

        reference operator*() const { return _it->second; }
        pointer operator->() const { return &_it->second; }

        value_iterator &operator++() {
            ++_it;
            return *this;
        }
        value_iterator operator++(int) {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const value_iterator &o) const { return _it == o._it; }
        bool operator!=(const value_iterator &o) const { return _it != o._it; }

        // Implicit conversion from iterator to const_iterator
        using const_value_iterator = value_iterator<typename container_type::const_iterator, const T &>;
        operator const_value_iterator() const { return const_value_iterator(_it); }

      private:
        MapIter _it;
    };

    using iterator       = value_iterator<typename container_type::iterator, T &>;
    using const_iterator = value_iterator<typename container_type::const_iterator, const T &>;

    // -- Capacity --------------------------------------------------------------
    virtual bool empty() const noexcept { return _data.empty(); }
    virtual size_t size() const noexcept { return _data.size(); }

    // -- Iteration (value iterators, not pair iterators) -----------------------
    virtual iterator begin() { return iterator(_data.begin()); }
    virtual const_iterator begin() const { return const_iterator(_data.begin()); }
    virtual iterator end() { return iterator(_data.end()); }
    virtual const_iterator end() const { return const_iterator(_data.end()); }

    // -- Lookup ----------------------------------------------------------------
    /// Throws std::out_of_range if key not found.
    virtual const T &at(const NSID &id) const { return _data.at(id); }
    virtual T &at(const NSID &id) { return _data.at(id); }

    /// Returns iterator or end().
    virtual iterator find(const NSID &id) { return iterator(_data.find(id)); }
    virtual const_iterator find(const NSID &id) const { return const_iterator(_data.find(id)); }

    virtual bool contains(const NSID &id) const noexcept { return _data.contains(id); }

    // -- Bulk access -----------------------------------------------------------
    virtual const container_type &data() const noexcept { return _data; }

    // -- Modifiers -------------------------------------------------------------
    virtual void clear() noexcept { _data.clear(); }

    /// Insert. Returns {iterator, true} on success, {iterator, false} on duplicate.
    virtual std::pair<iterator, bool> insert(const T &entry) {
        auto [it, inserted] = _data.try_emplace(entry.id, entry);
        return {iterator(it), inserted};
    }

    /// Insert or assign (overwrite if exists). Returns {iterator, true} if
    /// inserted, {iterator, false} if assigned.
    virtual std::pair<iterator, bool> insert_or_assign(const T &entry) {
        auto [it, inserted] = _data.insert_or_assign(entry.id, entry);
        return {iterator(it), inserted};
    }

    /// Erase by key. Returns true if the element was removed.
    virtual bool erase(const NSID &id) { return _data.erase(id) > 0; }

    /// Update if exists. Returns false if the key was not found.
    virtual bool update(const T &entry) {
        if (!_data.contains(entry.id))
            return false;
        _data.insert_or_assign(entry.id, entry);
        return true;
    }

    // -- Subset ----------------------------------------------------------------

    /// Create a new registry containing only entries matching the predicate.
    virtual IRegistry<T> create_subset(std::function<bool(const T &)> pred) const {
        IRegistry<T> subset;
        for (const auto &[id, entry] : _data)
            if (pred(entry))
                subset.insert(entry);
        return subset;
    }

  protected:
    container_type _data;
};
