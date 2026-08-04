# ── Integration / cross-domain tests ────────────────────────────────

besq_test_interface(test_integration test_integration.cpp)
besq_test_interface(test_algorithm_executor test_algorithm_executor.cpp)

# SolvePipeline 错误路径直驱（unknown_algo / unsupported_mode / 冲突不可解）
besq_test_interface(test_solve_pipeline test_solve_pipeline.cpp)

# 真实 localhost HTTP 端到端：HttpServer + WebModule + BesqContext
besq_test_interface(test_web_integration test_web_integration.cpp)
