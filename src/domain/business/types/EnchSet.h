#pragma once
#include "Ench.h"
#include "common/serialization/IJsonSerializable.h"
#include "common/utils/HashUtils.hpp"
#include <functional>
#include <unordered_set>

class EnchSet : public std::unordered_set<Ench>, public IJsonSerializable {
  public:
    using std::unordered_set<Ench>::unordered_set;

    iterator find(const NSID &ench_id);
    const_iterator find(const NSID &ench_id) const;

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;
};

template <> struct std::hash<EnchSet> {
    size_t operator()(const EnchSet &e) const noexcept {
        size_t h = 0;
        for (const auto &ench : e)
            hash_combine(h, std::hash<Ench>()(ench));
        return h;
    }
};
