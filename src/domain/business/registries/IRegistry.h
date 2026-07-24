#pragma once
#include "CommonTypes.h"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <vector>

// ══════════════════════════════════════════════════════════════════════════
// IRegistry<T> — uniform interface for business-domain registries
//
// All three registries (EnchantmentRegistry, EquipmentRegistry,
// EquipmentTagRegistry) implement this interface.
//
// Default implementations operate on _data (protected vector).
// Specialized registries override methods like insert(), get(), remove()
// to add key-map lookups.
// ══════════════════════════════════════════════════════════════════════════

template <typename T>
concept IRegistryItem = std::copyable<T> &&              // 可复制
                        std::movable<T> &&               // 可移动
                        std::default_initializable<T> && // 零参构造
                        std::equality_comparable<T> &&   // 相等比较（==, !=）
                        std::totally_ordered<T> &&       // 全序比较（<, >, <=, >=）
                        requires(T t) {
                            { std::hash<T>{}(t) } -> std::convertible_to<std::size_t>; // 可哈希
                            { t.id } -> std::convertible_to<NSID>;                     // 有 NSID 成员变量
                        };

template <IRegistryItem T, bool IsSorted = false> class IRegistry {
  public:
    virtual ~IRegistry() = default;

    using iterator               = std::vector<T>::iterator;
    using const_iterator         = std::vector<T>::const_iterator;
    static constexpr size_t nops = static_cast<size_t>(-1);

    /// Iterator access.
    virtual iterator begin() { return _data.begin(); }
    virtual const_iterator begin() const { return _data.begin(); }
    virtual iterator end() { return _data.end(); }
    virtual const_iterator end() const { return _data.end(); }

    /// Access by index (position in storage). Throws if out of range.
    virtual const T &at(size_t index) const { return _data.at(index); }
    virtual T &at(size_t index) { return _data.at(index); }

    /// Find the iterator of an item, or end() if not found.
    virtual const_iterator find(const NSID &id) const {
        if constexpr (IsSorted)
            return std::lower_bound(_data.begin(), _data.end(), id, [](const T &i) { return i.id; });
        else
            return std::find_if(_data.begin(), _data.end(), [id](const T &i) { return i.id == id; });
    }
    virtual iterator find(const NSID &id) {
        if constexpr (IsSorted)
            return std::lower_bound(_data.begin(), _data.end(), id, [](const T &i) { return i.id; });
        else
            return std::find_if(_data.begin(), _data.end(), [id](const T &i) { return i.id == id; });
    }

    /// Find the index of an item, or nops if not found.
    virtual size_t index(const NSID &id) const noexcept {
        auto it = find(id);
        if (it == _data.end())
            return nops;
        return std::distance(_data.begin(), it);
    }

    /// Membership test.
    virtual bool contains(const NSID &id) const noexcept { return find(id) != _data.end(); }

    /// Bulk access.
    virtual const std::vector<T> &data() const noexcept { return _data; }

    /// Query.
    virtual bool empty() const noexcept { return _data.empty(); }
    virtual size_t size() const noexcept { return _data.size(); }

    /// Reset all state.
    virtual void clear() noexcept { _data.clear(); }

    /// Utility.
    virtual void reverse() noexcept { std::reverse(_data.begin(), _data.end()); }
    virtual void resize(size_t n) noexcept { _data.resize(n); }
    virtual void sort() noexcept { std::sort(_data.begin(), _data.end()); }

    /// Insert a new item. Returns false if the item already exists.
    virtual bool insert(const T &item) {
        if constexpr (IsSorted) {
            auto it = find(item.id);
            if (it != _data.end())
                return false;
            _data.insert(it, item);
        } else {
            if (contains(item.id))
                return false;
            _data.push_back(item);
        }
        return true;
    }

    /// Remove by value. Returns false if not found.
    virtual bool remove(const NSID &id) {
        auto it = find(id);
        if (it == _data.end())
            return false;
        _data.erase(it);
        return true;
    }

    /// Update by value. Returns false if not found.
    virtual bool update(const T &item) {
        auto it = find(item.id);
        if (it == _data.end())
            return false;
        *it = item;
        return true;
    }

    /// Create a new registry containing only items matching filter_func.
    virtual IRegistry<T> create_subset(std::function<bool(const T &)> filter_func) const {
        IRegistry<T> subset;
        for (const auto &item : _data)
            if (filter_func(item))
                subset._data.push_back(item);
        subset.sort();
        return subset;
    }

  protected:
    std::vector<T> _data;
};
