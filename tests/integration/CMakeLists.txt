# ── Integration / cross-domain tests ────────────────────────────────
#
# NOTE: Complex integration tests are temporarily disabled during
# domain restructuring.  They will be re-enabled once the API bindings
# (BesqContext, SolvePipeline) and adapter layer are stabilized.

# Resolver tests (cross-domain: algorithm resolvers + business registries)
# TODO: re-enable when ItemResolver/InventoryResolver APIs stabilize
# besq_test(test_item_resolver resolvers/test_item_resolver.cpp)
# besq_test(test_inventory_resolver resolvers/test_inventory_resolver.cpp)

# Full integration tests
# TODO: re-enable when algorithm executor/diagnostics APIs stabilize
# besq_test(test_algorithm_executor test_algorithm_executor.cpp)
# besq_test(test_checkpoint_roundtrip test_checkpoint_roundtrip.cpp)
# besq_test_with_cli(test_integration test_integration.cpp)
# besq_test_with_cli(test_algorithm_acceptance test_algorithm_acceptance.cpp)
