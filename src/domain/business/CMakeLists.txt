# ── besq-domain-business ───────────────────────────────────────────────────
# Business (domain model) domain: user-facing game concept types and
# registries.  Depends only on besq-common.

set(BESQ_BUSINESS_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Enchantment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Equipment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Item.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/types/Solution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registries/EnchantmentRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registries/EquipmentCategoryRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registries/EquipmentRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registries/RegistryManager.cpp
)

add_library(besq-domain-business STATIC ${BESQ_BUSINESS_SRCS})

target_link_libraries(besq-domain-business PUBLIC besq-common)

target_include_directories(besq-domain-business PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>       # → #include "types/Enchantment.h"
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>            # → #include "common/io/json.h"
)
