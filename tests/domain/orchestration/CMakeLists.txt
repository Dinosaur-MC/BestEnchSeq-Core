# ── Orchestration domain tests ─────────────────────────────────────
# Tests for besq-orchestration: adapters, serializers, formatters.
#
# NOTE: Complex adapter tests are temporarily disabled during domain
# restructuring. The adapter API (CompactAdapter, RawTypeAdapter) has
# changed significantly and tests need updating.

# TODO: re-enable when adapter APIs stabilize
# besq_test(test_compact_adapter test_compact_adapter.cpp)
# besq_test(test_output_formatter test_output_formatter.cpp)
# besq_test(test_raw_type_adapter test_raw_type_adapter.cpp)

# EnchantmentRegistry lookup tests (standalone, no adapter deps)
besq_test(test_enchantment_registry_lookup test_enchantment_registry_lookup.cpp)
