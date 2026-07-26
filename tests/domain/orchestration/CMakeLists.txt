# ── Orchestration domain tests ─────────────────────────────────────
# Tests for besq-domain-orchestration: adapters, serializers, formatters.

besq_test(test_output_formatter test_output_formatter.cpp)
besq_test(test_compact_adapter test_compact_adapter.cpp)

# EnchantmentRegistry lookup tests (standalone, no adapter deps)
besq_test(test_enchantment_registry_lookup test_enchantment_registry_lookup.cpp)

# ExportPipeline tests
besq_test(test_export_pipeline test_export_pipeline.cpp)

# TODO: Re-enable after migrating to new RegistryLoader
# besq_test(test_raw_type_adapter test_raw_type_adapter.cpp)
