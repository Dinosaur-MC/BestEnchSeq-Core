# ── Business domain tests ───────────────────────────────────────────
# Tests for besq-domain-business: registries, domain types.

besq_test(test_enchantment_registry test_enchantment_registry.cpp)
besq_test(test_equipment_registry test_equipment_registry.cpp)
besq_test(test_enchantment_registry_mutable test_enchantment_registry_mutable.cpp)
besq_test(test_equipment_registry_mutable test_equipment_registry_mutable.cpp)
besq_test(test_equipment_category_registry test_equipment_category_registry.cpp)
besq_test(test_domain_types test_domain_types.cpp)
besq_test(test_registry_copy_move test_registry_copy_move.cpp)
besq_test(test_registry_subset_bulk test_registry_subset_bulk.cpp)
besq_test(test_registry_edge test_registry_edge.cpp)
besq_test(test_serializer test_serializer.cpp)
besq_test(test_profile_manager test_profile_manager.cpp)
besq_test(test_registry_helper test_registry_helper.cpp)
besq_test(test_registry_loader test_registry_loader.cpp)
besq_test(test_parser_detail test_parser_detail.cpp)
besq_test(test_parsers test_parsers.cpp)

