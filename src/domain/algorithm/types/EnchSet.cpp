#include "EnchSet.h"
#include "common/utils/HashUtils.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>

namespace algorithm {

EnchSet::EnchSet(std::initializer_list<Ench> il) noexcept {
    for (const auto &ench : il)
        insert(ench);
}

// ── Iterators ──
EnchSet::iterator EnchSet::begin() noexcept {
    return empty() ? end() : iterator(this, std::countr_zero(_mask));
}
EnchSet::iterator EnchSet::end() noexcept { return iterator(this, MAX_SIZE); }
EnchSet::const_iterator EnchSet::begin() const noexcept {
    return empty() ? end() : const_iterator(this, std::countr_zero(_mask));
}
EnchSet::const_iterator EnchSet::end() const noexcept { return const_iterator(this, MAX_SIZE); }

// ── Capacity ──
size_t EnchSet::size() const noexcept { return _size; }
bool EnchSet::empty() const noexcept { return _mask == 0; }

// ── Lookup ──
EnchSet::iterator EnchSet::find(const Ench &ench) noexcept {
    if (ench.id >= npos || !contains(ench.id))
        return end();
    return iterator(this, ench.id);
}
EnchSet::const_iterator EnchSet::find(const Ench &ench) const noexcept {
    if (ench.id >= npos || !contains(ench.id))
        return end();
    return const_iterator(this, ench.id);
}
EnchSet::iterator EnchSet::find(const EnchRef &ench) noexcept {
    if (ench.id() >= npos || !contains(ench.id()))
        return end();
    return iterator(this, ench.id());
}
EnchSet::const_iterator EnchSet::find(const ConstEnchRef &ench) const noexcept {
    if (ench.id() >= npos || !contains(ench.id()))
        return end();
    return const_iterator(this, ench.id());
}

EnchSet::value_type EnchSet::at(value_type id) const {
    if (id >= npos || !contains(id))
        throw std::out_of_range("EnchSet::at: id out of range");
    return _lvls[id];
}
EnchSet::value_type EnchSet::operator[](value_type id) noexcept {
    return id < npos ? _lvls[id] : value_type{};
}
EnchSet::value_type EnchSet::operator[](value_type id) const noexcept {
    return id < npos ? _lvls[id] : value_type{};
}
bool EnchSet::contains(value_type id) const noexcept { return _lvls[id] > 0; }
EnchSet::value_type EnchSet::first() const noexcept { return _mask ? std::countr_zero(_mask) : npos; }
EnchSet::value_type EnchSet::next(EnchSet::value_type id) const noexcept {
    auto dis = std::countr_zero(_mask >> (id + value_type{1}));
    return dis < npos ? dis + id + 1 : npos;
}
EnchSet::value_type EnchSet::next_level(EnchSet::value_type id) const noexcept {
    auto next_id = next(id);
    return next_id < npos ? _lvls[next_id] : value_type{};
}
std::span<const EnchSet::value_type, EnchSet::MAX_SIZE> EnchSet::data() const noexcept {
    return std::span<const EnchSet::value_type, EnchSet::MAX_SIZE>(_lvls, MAX_SIZE);
}

// ── Modifiers ──
bool EnchSet::insert(const Ench &ench) noexcept {
    if (ench.id >= npos || ench.level <= 0)
        return false;
    bool exists = contains(ench.id);
    _mask |= (mask_type{1} << ench.id);
    _lvls[ench.id] = ench.level;
    if (!exists)
        _size++;
    _hash_cache = 0;
    return !exists;
}
bool EnchSet::insert(value_type id, value_type level) noexcept {
    if (id >= npos || level <= 0)
        return false;
    bool exists = contains(id);
    _mask |= (mask_type{1} << id);
    _lvls[id] = level;
    if (!exists)
        _size++;
    _hash_cache = 0;
    return !exists;
}
bool EnchSet::erase(value_type id) noexcept {
    if (id >= npos || !contains(id))
        return false;
    _mask &= ~(mask_type{1} << id);
    _lvls[id] = 0;
    _size--;
    _hash_cache = 0;
    return true;
}
bool EnchSet::erase(iterator pos) noexcept {
    if (pos != end())
        return erase((*pos).id());
    return false;
}
void EnchSet::clear() noexcept {
    _mask = 0;
    _size = 0;
    std::memset(_lvls, 0, sizeof(_lvls));
    _hash_cache = 0;
}

// ── Hash (lazily cached) ──
size_t EnchSet::hash() const noexcept {
    if (_hash_cache == 0 && _mask != 0) {
        size_t h = _mask;
        for (size_t off = 0; off < sizeof(_lvls); off += sizeof(size_t)) {
            size_t word = 0;
            memcpy(&word, _lvls + off, sizeof(word));
            hash_combine(h, word);
        }
        _hash_cache = h ? h : _mask;
    }
    return _hash_cache;
}
void EnchSet::rehash() const noexcept {
    _hash_cache = 0;
    (void)hash();
}

// ── Comparison ──
bool EnchSet::operator==(const EnchSet &o) const noexcept {
    return _mask == o._mask && std::memcmp(_lvls, o._lvls, sizeof(_lvls)) == 0;
}

// ── Serialization ──
void EnchSet::serialize(ByteStreamWriter &w) const noexcept {
    w << _mask;
    w.bytes(_lvls, sizeof(_lvls));
}
void EnchSet::deserialize(ByteStreamReader &r) noexcept {
    clear();
    r >> _mask;
    auto lvls_data = r.read_bytes(sizeof(_lvls));
    if (!lvls_data.empty()) {
        _size = std::popcount(_mask);
        memcpy(_lvls, lvls_data.data(), sizeof(_lvls));
    }
}
} // namespace algorithm
