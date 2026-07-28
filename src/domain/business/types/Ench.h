#pragma once
#include "common/CommonTypes.h"
#include "common/serialization/IJsonSerializable.h"
#include "common/utils/HashUtils.hpp"
#include <string>

struct Ench : IJsonSerializable {
    NSID id;
    // @deprecated  Display name (use ench_display_name() / item_display_name()
    //              from NsidDisplay.h instead).  Kept for serialization compat.
    std::string name;
    int32_t level = 1;

    Ench() = default;
    // Note: name parameter kept for backward compat, callers should pass ""
    Ench(NSID id_, std::string name_, int32_t level_ = 1)
        : id(std::move(id_)), name(std::move(name_)), level(level_) {}

    bool operator==(const Ench &o) const noexcept { return id == o.id && level == o.level; }
    bool operator<(const Ench &o) const noexcept {
        if (id == o.id)
            return level < o.level;
        return id < o.id;
    }

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;
};

template <> struct std::hash<Ench> {
    size_t operator()(const Ench &e) const noexcept {
        size_t h = std::hash<NSID>()(e.id);
        hash_combine(h, e.level);
        return h;
    }
};
