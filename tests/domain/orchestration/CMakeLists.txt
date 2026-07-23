# ── Orchestration domain tests ─────────────────────────────────────
# Tests for besq-orchestration: adapters, serializers, formatters.
#
# Orchestration domain tests — adapters, serializers, formatters.
besq_test(test_raw_type_adapter test_raw_type_adapter.cpp)
besq_test(test_output_formatter test_output_formatter.cpp)
besq_test(test_compact_adapter test_compact_adapter.cpp)

# EnchantmentRegistry lookup tests (standalone, no adapter deps)
besq_test(test_enchantment_registry_lookup test_enchantment_registry_lookup.cpp)
