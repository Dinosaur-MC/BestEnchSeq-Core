# ── Business domain tests ───────────────────────────────────────────
# Tests for besq-domain-business: registries, domain types.

besq_test(test_enchantment_registry test_enchantment_registry.cpp)
besq_test(test_equipment_registry test_equipment_registry.cpp)
besq_test(test_enchantment_registry_mutable test_enchantment_registry_mutable.cpp)
besq_test(test_equipment_registry_mutable test_equipment_registry_mutable.cpp)
besq_test(test_equipment_category_registry test_equipment_category_registry.cpp)
besq_test(test_domain_types test_domain_types.cpp)

# RegistryManager test needs its dedicated source (not fully in besq-domain-business)
add_executable(test_registry_manager
    test_registry_manager.cpp
    ${PROJECT_ROOT}/src/domain/business/registries/RegistryManager.cpp
)
target_include_directories(test_registry_manager PRIVATE ${TEST_INCLUDE} ${PROJECT_ROOT}/include)
target_link_libraries(test_registry_manager PRIVATE besq-core)
add_test(NAME test_registry_manager COMMAND test_registry_manager)
set_tests_properties(test_registry_manager PROPERTIES
    WORKING_DIRECTORY ${PROJECT_ROOT}
)
