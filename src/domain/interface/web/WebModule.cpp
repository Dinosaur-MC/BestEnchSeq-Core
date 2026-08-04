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
#include <utility>

namespace web {

/// WebModule 会话状态。`_ctx_gate` 序列化 solve worker（WebSolveService 持有它
/// 覆盖整个 _ctx 访问窗口）与 server 线程上的 profile 变更——两侧都经
/// resolve_effective() 触碰 ProfileManager 未加锁的有效视图缓存。`_solve` 声明在
/// `_ctx_gate`/`_hub` 之后，ctor 里把两者按引用传给 WebSolveService。
struct WebModule::Impl {
    BesqContext& _ctx;
    std::mutex _ctx_gate;
    SseHub _hub;
    webhttp::WebSolveService _solve;
    Router _router;
    StaticFileServer _sfs;

    explicit Impl(BesqContext& ctx)
        : _ctx(ctx), _solve(ctx, _ctx_gate, &_hub) {}
};

WebModule::WebModule(BesqContext& ctx) : _impl(std::make_unique<Impl>(ctx)) {
    _impl->_router.register_controller<HealthController>();
    _impl->_router.register_controller<StatusController>(ctx);
    _impl->_router.register_controller<SettingsController>(ctx);
    _impl->_router.register_controller<ProfilesController>(ctx, _impl->_ctx_gate);
    _impl->_router.register_controller<AlgorithmController>(ctx, _impl->_solve);
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
