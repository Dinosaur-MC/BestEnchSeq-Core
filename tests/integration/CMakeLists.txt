# ── Integration / cross-domain tests ────────────────────────────────
#
# NOTE: Disabled until interface domain refactoring completes.
# Integration tests depend on:
#   - ItemResolver / InventoryResolver (interface → algorithm bridge)
#   - CLI pipeline (parse → resolve → solve → format)
#   - Algorithm executor with diagnostics
#   - BesqContext / SolvePipeline API

# TODO: re-enable when interface domain is fully refactored
# besq_test(test_item_resolver resolvers/test_item_resolver.cpp)
# besq_test(test_inventory_resolver resolvers/test_inventory_resolver.cpp)
# besq_test(test_algorithm_executor test_algorithm_executor.cpp)
# besq_test(test_checkpoint_roundtrip test_checkpoint_roundtrip.cpp)
# besq_test(test_integration test_integration.cpp)
# besq_test(test_algorithm_acceptance test_algorithm_acceptance.cpp)
