#include "CalculatorController.h"
#include "domain/interface/web/WebSolveService.h"
#include "domain/interface/web/SseHub.h"
#include "domain/interface/web/WebSchema.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/components/http/StreamChannel.h"
#include "ds/Error.h"
#include "common/io/json.h"
#include <string>

namespace web {

namespace {
const char* state_name(TaskState s) {
    switch (s) {
        case TaskState::Running:   return "running";
        case TaskState::Completed: return "completed";
        case TaskState::Failed:    return "failed";
        case TaskState::Cancelled: return "cancelled";
    }
    return "unknown";
}

/// SSE 帧（与 WebSolveService 发布的帧同格式，spec §7）：
/// `event: <type>\ndata: <json>\n\n`。
std::string sse_frame(const std::string& type, const Json& payload) {
    return "event: " + type + "\ndata: " + payload.to_string() + "\n\n";
}
} // namespace

Response CalculatorController::submit(const HttpRequest&, const PathParams&, const Json& body) {
    WebTaskDto dto;
    try {
        WebTaskJson::parse_or_throw(body, dto);
    } catch (const ds::ValidationError&) {
        // Schema violation (missing/wrong-typed field) — 400 INVALID_TASK.
        // (Router only maps JsonException→400 INVALID_BODY, so ValidationError
        // must be converted here or it would surface as a 500.)
        throw WebHttpError(400, "INVALID_TASK", "invalid task body");
    } catch (const JsonException&) {
        throw WebHttpError(400, "INVALID_TASK", "invalid task JSON");
    }
    // start() throws WebHttpError(409, TASK_ACTIVE) on active-slot conflict;
    // the Router's dispatch catch maps it to a 409 response.
    std::string id = _svc.start(dto);
    Json o = Json::object();
    o["task_id"] = Json(id);
    return Response::accepted("/api/tasks/" + id, o.to_string());
}

Response CalculatorController::status(const HttpRequest&, const PathParams& pp) {
    auto st = _svc.status(pp.get("id"));  // 404 when unknown
    Json o = Json::object();
    o["state"] = Json(state_name(st.state));
    o["progress"] = Json(st.progress);
    if (st.state == TaskState::Completed)
        o["result"] = Json::parse(st.result);
    else if (st.state == TaskState::Failed)
        o["error"] = Json(st.error);
    return Response::json(200, "OK", o.to_string());
}

Response CalculatorController::cancel(const HttpRequest&, const PathParams& pp) {
    const std::string id = pp.get("id");
    // status() distinguishes "unknown" (404) from "already finished" — the
    // service's cancel() bool alone cannot tell them apart. DELETE on a
    // finished task is a successful no-op.
    (void)_svc.status(id);   // 404 when unknown
    _svc.cancel(id);
    Json o = Json::object();
    o["ok"] = Json(true);
    return Response::json(200, "OK", o.to_string());
}

Response CalculatorController::events(const HttpRequest& req, const PathParams& pp) {
    const std::string id = pp.get("id");
    // Validate the task exists before opening a stream — unknown → 404.
    (void)_svc.status(id);   // 404 when unknown
    // 订阅 SseHub 并把每一帧投递到请求的 StreamChannel（连接）。真实传输路径上
    // req.stream 恒为连接（Connection 实现 StreamChannel）；单元测试直调时可能为空，
    // 此时订阅仍注册但帧被静默丢弃。
    auto ch = req.stream;
    auto sub = _hub.subscribe(id, [ch](const std::string&, std::string frame) {
        if (ch) ch->post_frame(std::move(frame));
    });
    _streams[id] = sub;
    // 迟到订阅者（如 SPA 重连）立即收到任务的最新进度帧，而非直到下一次 publish
    // 才有任何输出（spec §7 帧形状：{"type":"progress","progress":<p>}）。任务已完成
    // 时 status() 返回 progress=1.0 —— 客户端随后收到 completed 帧即知终态。
    if (ch) {
        auto st = _svc.status(id);
        Json obj = Json::object();
        obj["type"] = Json("progress");
        obj["progress"] = Json(st.progress);
        ch->post_frame(sse_frame("progress", obj));
    }
    // 连接关闭（客户端断开/服务端关闭）时退订：SseHub 不再持有死连接的帧回调，
    // 避免连接及其帧汇永久存活（每次 publish 白费功夫 + 长期泄漏）。不捕获 ch ——
    // on_close 存于连接自身，捕获自身会形成 shared_ptr 环；只捕获 this + id + SubId。
    // SseHub::unsubscribe 幂等：即使任务已被 unsubscribe_all 清空也能安全调用。
    if (ch) {
        ch->on_close([this, id, sub] {
            _hub.unsubscribe(id, sub);
            auto it = _streams.find(id);
            if (it != _streams.end() && it->second == sub)
                _streams.erase(it);
        });
    }
    return sse_stream_response();
}

} // namespace web
