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
besq_test_interface(test_web_solve_sse test_web_solve_sse.cpp)

# components/http framework tests
besq_test_interface(test_http_common test_http_common.cpp)
besq_test_interface(test_socket_nb test_socket_nb.cpp)
besq_test_interface(test_route_def test_route_def.cpp)
besq_test_interface(test_http_parser test_http_parser.cpp)
besq_test_interface(test_static_file_server test_static_file_server.cpp)
besq_test_interface(test_router test_router.cpp)
besq_test_interface(test_connection test_connection.cpp)
besq_test_interface(test_sse_stream test_sse_stream.cpp)
