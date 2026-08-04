# ── Interface domain tests ──────────────────────────────────────────
# Tests for besq-domain-interface: CLI, parsers, API.

# Parser tests — link interface domain (besq-core excludes interface)
besq_test_interface(test_ench_parser test_ench_parser.cpp)
besq_test_interface(test_item_parser test_item_parser.cpp)
besq_test_interface(test_inventory_parser test_inventory_parser.cpp)
besq_test_interface(test_cli_parser test_cli_parser.cpp)
besq_test_interface(test_cli_acceptance test_cli_acceptance.cpp)


besq_test_interface(test_besq_context test_besq_context.cpp)

# Web module tests (interface domain)
besq_test_interface(test_http_server test_http_server.cpp)
besq_test_interface(test_web_api test_web_api.cpp)
besq_test_interface(test_web_schema test_web_schema.cpp)
besq_test_interface(test_web_calculator test_web_calculator.cpp)
