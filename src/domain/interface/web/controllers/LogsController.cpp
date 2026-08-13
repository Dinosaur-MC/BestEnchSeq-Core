#include "LogsController.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/components/http/StreamChannel.h"
#include "domain/interface/web/SseHub.h"
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
} // namespace

Response LogsController::tail(const HttpRequest& req) {
    int64_t since = 0;
    if (req.query.has("since")) {
        if (!parse_i64(req.query.get("since"), since) || since < 0)
            throw WebHttpError(400, "INVALID_FIELD", "since must be a non-negative integer");
    }
    bool has_limit = req.query.has("limit");
    int64_t limit = 0;
    if (has_limit) {
        if (!parse_i64(req.query.get("limit"), limit) || limit < 0)
            throw WebHttpError(400, "INVALID_FIELD", "limit must be a non-negative integer");
    }
    // B1 起 Logger 不再有 ring：tail 恒为空（{"logs":[],"next":since}）。
    // 本端点由 B3 删除（/api/history 替代）；参数校验契约保留。
    Json root = Json::object();
    root["logs"] = Json::array();
    root["next"] = Json(since);
    return Response::json(200, "OK", root.to_string());
}

Response LogsController::events(const HttpRequest& req) {
    // 订阅 SseHub 的合成 "logs" key 并把每一帧投递到请求的 StreamChannel（连接）。
    // 真实传输路径上 req.stream 恒为连接；单元测试直调时可能为空 → 帧静默丢弃。
    auto ch = req.stream;
    auto sub = _hub.subscribe("logs", [ch](const std::string&, std::string frame) {
        if (ch)
            ch->post_frame(std::move(frame));
    });
    // 连接关闭时退订（与 CalculatorController::events 相同，见其注释）。
    // 不捕获 ch：on_close 存于连接自身，捕获自身会形成 shared_ptr 环。
    if (ch) {
        ch->on_close([this, sub] { _hub.unsubscribe("logs", sub); });
    }
    return sse_stream_response();
}

} // namespace web
