#include "HistoryController.h"
#include "common/io/json.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/Router.h"
#include <cerrno>
#include <cstdlib>
#include <string>

namespace web {

namespace {
/// Strict int64 parse: empty / trailing junk / overflow → false.
bool parse_i64(const std::string& s, int64_t& out) {
    if (s.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE || end == s.c_str() || *end != '\0')
        return false;
    out = static_cast<int64_t>(v);
    return true;
}

/// 事件类型序列化为小写字符串（与 SSE 帧 type 命名一致，前端枚举同源）。
const char* solve_type_name(SolveEventType t) {
    switch (t) {
    case SolveEventType::Submitted:
        return "submitted";
    case SolveEventType::Completed:
        return "completed";
    case SolveEventType::Failed:
        return "failed";
    case SolveEventType::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

/// 单条事件序列化为 JSON 对象：SolveHistoryEvent 全字段，无关类型字段填缺省值
/// （Completed 外的 total_level_cost 等为 0，Failed 外的 error_message 为空串）。
/// result 字段（C1）：Completed 事件为完整结果 JSON 对象（parse 失败回退字符串），
/// 其余类型为 null。
Json event_to_json(const SolveHistoryEvent& ev) {
    Json o = Json::object();
    o["seq"] = Json(static_cast<int64_t>(ev.seq));
    o["type"] = Json(solve_type_name(ev.type));
    o["task_id"] = Json(ev.task_id);
    o["target"] = Json(ev.target);
    o["algorithm"] = Json(ev.algorithm);
    o["mode"] = Json(ev.mode);
    o["timestamp_ms"] = Json(ev.timestamp_ms);
    o["total_level_cost"] = Json(ev.total_level_cost);
    o["total_exp_cost"] = Json(ev.total_exp_cost);
    o["solution_count"] = Json(ev.solution_count);
    o["computation_ms"] = Json(ev.computation_ms);
    o["error_message"] = Json(ev.error_message);
    if (!ev.result_json.empty()) {
        try {
            o["result"] = Json::parse(ev.result_json);
        } catch (const JsonException&) {
            o["result"] = Json(ev.result_json); // 解析失败回退原始字符串
        }
    } else {
        o["result"] = Json::null();
    }
    return o;
}
} // namespace

Response HistoryController::list(const HttpRequest& req) {
    // 参数解析与校验：offset/limit/after_seq 均为非负整数，非法 → 400
    // INVALID_FIELD（沿用 LogsController 既有 400 契约）。
    int64_t offset = 0;
    if (req.query.has("offset")) {
        if (!parse_i64(req.query.get("offset"), offset) || offset < 0)
            throw WebHttpError(400, "INVALID_FIELD", "offset must be a non-negative integer");
    }
    int64_t limit = 100; // 默认最近 100 条
    if (req.query.has("limit")) {
        if (!parse_i64(req.query.get("limit"), limit) || limit < 0)
            throw WebHttpError(400, "INVALID_FIELD", "limit must be a non-negative integer");
    }
    if (limit > 200)
        limit = 200;        // 上限封顶（非错误，裁剪到上限）
    int64_t after_seq = -1; // 未指定 → 不过滤
    if (req.query.has("after_seq")) {
        if (!parse_i64(req.query.get("after_seq"), after_seq) || after_seq < 0)
            throw WebHttpError(400, "INVALID_FIELD", "after_seq must be a non-negative integer");
    }

    // 快照（最新在前）；total 恒为上下文全部事件数（与查询参数无关）。
    const auto hist = _ctx.solve_history();
    const int64_t total = static_cast<int64_t>(hist.size());

    // 过滤（after_seq 游标）→ 切片（offset 跳过 / limit 截断），保持最新在前。
    Json events = Json::array();
    int64_t skipped = 0, taken = 0;
    for (const auto& ev : hist) {
        if (after_seq >= 0 && static_cast<int64_t>(ev.seq) <= after_seq)
            continue; // 只保留 seq > after_seq 的新事件
        if (skipped < offset) {
            ++skipped;
            continue; // offset：跳过过滤结果的前 N 条
        }
        if (taken >= limit)
            break;
        events.push_back(event_to_json(ev));
        ++taken;
    }

    Json root = Json::object();
    root["events"] = std::move(events);
    root["total"] = Json(total);
    root["next_offset"] = Json(offset + taken); // 下一页起点；无更多页时等于 offset
    return Response::json(200, "OK", root.to_string());
}

} // namespace web
