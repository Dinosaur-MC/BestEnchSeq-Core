#include "WebDiagObserver.h"
#include "domain/algorithm/diagnostics/DiagnosticsWriter.h"
#include <utility>
#include <vector>

namespace web {

namespace {

const char* state_name(algorithm::AlgorithmState s) {
    switch (s) {
        case algorithm::AlgorithmState::Idle:      return "idle";
        case algorithm::AlgorithmState::Running:   return "running";
        case algorithm::AlgorithmState::Pausing:   return "pausing";
        case algorithm::AlgorithmState::Paused:    return "paused";
        case algorithm::AlgorithmState::Completed: return "completed";
        case algorithm::AlgorithmState::Failed:    return "failed";
        case algorithm::AlgorithmState::Cancelled: return "cancelled";
    }
    return "unknown";
}

/// DiagnosticsWriter::Entry 的值（int64_t | std::string）→ Json。
Json entry_to_json(const DiagnosticsWriter::Entry& e) {
    return std::visit([](const auto& v) -> Json { return Json(v); }, e.value);
}

} // namespace

void WebDiagObserver::on_progress(size_t, uint8_t pct,
                                  algorithm::ProgressStatus status) {
    Json obj = Json::object();
    obj["kind"] = Json("progress");
    obj["status"] = Json(std::string(algorithm::to_string(status)));
    obj["pct"] = Json(static_cast<int64_t>(pct));
    _on_event(std::move(obj));
}

void WebDiagObserver::on_state_changed(size_t, algorithm::AlgorithmState prev,
                                       algorithm::AlgorithmState curr) {
    Json obj = Json::object();
    obj["kind"] = Json("state");
    obj["from"] = Json(state_name(prev));
    obj["to"] = Json(state_name(curr));
    _on_event(std::move(obj));
}

void WebDiagObserver::on_exit(size_t, std::string_view algorithm_name,
                              const algorithm::DiagnosticsEvent::ExitPayload& payload) {
    Json obj = Json::object();
    obj["kind"] = Json("exit");
    obj["algorithm"] = Json(std::string(algorithm_name));
    obj["status"] = Json(payload.status);
    obj["wall_ms"] = Json(payload.wall_ms);

    // Tier-2 逐操作计数器（BESQ_DEEP_DIAGNOSTICS 才编译进；非搜索策略恒为 0）。
    // 与 DiagnosticsService 的文件持久化不同（全零时整组省略），前端消费的
    // 形状固定：counters 永远携带三个键。
    Json counters = Json::object();
    counters["nodes_visited"] = entry_to_json(payload.nodes_visited);
    counters["nodes_pruned"] = entry_to_json(payload.nodes_pruned);
    counters["steps_forged"] = entry_to_json(payload.steps_forged);
    obj["counters"] = std::move(counters);

    // AlgorithmDiagnostics::flush 展平的 KV（镜像文件持久化的字段集；同样
    // 去掉 "status"——顶层 status 已由 payload.status 提供，避免重复）。
    std::vector<DiagnosticsWriter::Entry> entries;
    if (payload.diagnostics)
        payload.diagnostics->flush(entries);
    Json diag = Json::object();
    for (const auto& e : entries) {
        if (std::string_view(e.key) == "status")
            continue;
        diag[e.key] = entry_to_json(e);
    }
    obj["diag"] = std::move(diag);

    _on_event(std::move(obj));
}

} // namespace web
