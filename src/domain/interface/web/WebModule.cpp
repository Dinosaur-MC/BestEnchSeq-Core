#include "WebModule.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/http/Router.h"
#include "domain/interface/web/controllers/HealthController.h"
#include "domain/interface/web/controllers/StatusController.h"
#include "domain/interface/web/controllers/SettingsController.h"
#include "domain/interface/web/controllers/ProfilesController.h"
#include "domain/interface/web/controllers/AlgorithmController.h"
#include "domain/interface/web/controllers/CalculatorController.h"
#include "domain/interface/web/controllers/LogsController.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "common/log/LogRingBuffer.h"
#include <string>
#include <utility>

namespace web {

namespace {

/// 日志级别 → SSE 帧内 level 字符串（与 LogsController::tail 的增量契约一致）。
const char* log_level_name(LogLevel lv) {
    switch (lv) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
    }
    return "unknown";
}

/// 一条日志 → 单条 SSE data 帧。用无 event 名的纯 data 帧（前端 logs.js 用
/// `es.onmessage` 消费——SSE 规范里带 event 名的帧不会触发 onmessage）。
std::string log_sse_frame(const LogRecord& e) {
    Json obj = Json::object();
    obj["seq"] = Json(e.timestamp_ms);          // 增量游标 = 毫秒时间戳（与 tail 一致）
    obj["level"] = Json(log_level_name(e.level));
    obj["timestamp_ms"] = Json(e.timestamp_ms);
    obj["message"] = Json(e.message);
    Json root = Json::object();
    root["logs"] = Json::array();
    root["logs"].push_back(obj);
    return "data: " + root.to_string() + "\n\n";
}

} // namespace

/// WebModule 会话状态。`_ctx_gate` 序列化 solve worker（WebSolveService 持有它
/// 覆盖整个 _ctx 访问窗口）与 server 线程上的 profile 变更——两侧都经
/// resolve_effective() 触碰 ProfileManager 未加锁的有效视图缓存。`_solve` 声明在
/// `_ctx_gate`/`_hub` 之后，ctor 里把两者按引用传给 WebSolveService。
struct WebModule::Impl {
    BesqContext& _ctx;
    std::mutex _ctx_gate;
    SseHub _hub;
    web::WebSolveService _solve;
    Router _router;
    StaticFileServer _sfs;
    std::shared_ptr<LogRingBuffer> _logs_ring;      // 日志 live 源（可为空）
    LogRingBuffer::ListenerId _logs_listener = 0;   // 注册的监听器 token

    explicit Impl(BesqContext& ctx)
        : _ctx(ctx), _solve(ctx, _ctx_gate, &_hub) {
        // I-1：/api/logs/events 实时尾——把 LogRingBuffer 的每次 push 转成 SSE
        // 帧发布到 hub 的合成 "logs" key。注册恰一次（构造期）；析构时移除。
        // 监听器捕获裸 `this`（Impl）：LogRingBuffer 的 remove_listener 与在途
        // 调用同步（push 持锁调用监听器），故 Impl 析构先移除监听器再释放成员，
        // 不会出现对已析构 Impl 的调用。
        _logs_ring = Logger::instance().ring_buffer();
        if (_logs_ring)
            _logs_listener = _logs_ring->add_listener(
                [this](const LogRecord& e) { _hub.publish("logs", log_sse_frame(e)); });
    }

    ~Impl() {
        if (_logs_ring && _logs_listener != 0)
            _logs_ring->remove_listener(_logs_listener);
    }
};

WebModule::WebModule(BesqContext& ctx) : _impl(std::make_unique<Impl>(ctx)) {
    _impl->_router.register_controller<HealthController>();
    _impl->_router.register_controller<StatusController>(ctx, _impl->_ctx_gate);
    _impl->_router.register_controller<SettingsController>(_impl->_ctx_gate);
    _impl->_router.register_controller<ProfilesController>(ctx, _impl->_ctx_gate);
    _impl->_router.register_controller<AlgorithmController>(ctx, _impl->_solve, _impl->_ctx_gate);
    _impl->_router.register_controller<CalculatorController>(_impl->_solve, _impl->_hub);
    _impl->_router.register_controller<LogsController>(ctx, _impl->_hub);
}

WebModule::~WebModule() {
    if (!_impl) return;
    // 关机排序修复（两处潜在 UAF）：
    //   (a) controllers 的 on_close 捕获 `this`；Impl 成员析构序 `_router`(控制器) →
    //       `_solve`(join worker) → `_hub`。若 SseHub 仍持有订阅（捕获
    //       shared_ptr<Connection> → 连接 on_close 捕获控制器 this），`_router` 先死
    //       会让连接 close() 触发在已析构控制器上。
    //   (b) Reactor::add_connection 的帧汇捕获裸 `Reactor* this`；HttpServer 在 main
    //       中声明于 WebModule 之后 → 先析构 → Reactor 先死。`_solve` dtor join
    //       worker 期间 worker 的 publish 会经 hub 订阅 → 连接 post_frame → 帧汇 →
    //       已析构 Reactor 的 loop.post。
    // 在 Impl 成员析构前清空 hub：此刻控制器与 hub 均存活，触发的 on_close 安全；
    // 清空后 worker publish 无订阅可送达，帧汇窗口关闭。
    _impl->_hub.clear();
}

void WebModule::set_static_resources(std::map<std::string, StaticResource> embedded) {
    _impl->_sfs.mount_embedded("/public", std::move(embedded));
}

void WebModule::mount_res_dir(std::filesystem::path root) {
    _impl->_sfs.mount_disk("/public", std::move(root));
}

HttpResponse WebModule::dispatch(const HttpRequest& req) {
    // `/` → SPA 入口重定向。
    if (req.path == "/") {
        HttpResponse r = HttpResponse::json(307, "Temporary Redirect", "");
        r.headers.emplace_back("Location", "/public/index.html");
        return r;
    }
    // `/public/*` → 静态资源（嵌入式优先，磁盘兜底）。
    if (req.path.rfind("/public", 0) == 0)
        return _impl->_sfs.serve(req.method, req.path);
    // 其余 → 控制器路由（/health, /api/*）。
    return _impl->_router.dispatch(req);
}

} // namespace web
