#include "Ench.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"

#include <stdexcept>

Ench::Ench(int32_t id) : id(id), level(1) {
    if (id < 0 || id >= static_cast<int32_t>(registries::enchants().size()))
        throw std::out_of_range("Invalid Ench id");
}
Ench::Ench(int32_t id, int32_t level) : id(id), level(level) {
    if (id < 0 || id >= static_cast<int32_t>(registries::enchants().size()))
        throw std::out_of_range("Invalid Ench id");
}

Ench Ench::checked(int32_t id, int32_t level, const EnchantmentRegistry& reg) {
    if (id < 0 || id >= static_cast<int32_t>(reg.size()))
        throw std::out_of_range("Invalid Ench id");
    return Ench(id, level, unchecked);
}
