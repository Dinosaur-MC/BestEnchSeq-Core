# ── Interface domain tests ──────────────────────────────────────────
# Tests for besq-domain-interface: CLI, parsers, API.

# Parser tests — link full besq-core for registry types
besq_test(test_ench_parser test_ench_parser.cpp)
besq_test(test_item_parser test_item_parser.cpp)
besq_test(test_cli_parser test_cli_parser.cpp)
besq_test(test_cli_acceptance test_cli_acceptance.cpp)


besq_test(test_besq_context test_besq_context.cpp)
