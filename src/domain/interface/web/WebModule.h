#pragma once
#include "domain/interface/components/http/HttpCommon.h"
#include "domain/interface/components/http/StaticFileServer.h"
#include "domain/interface/web/WebSolveService.h"
#include <filesystem>
#include <map>
#include <memory>
#include <string>

class BesqContext;

namespace web {

/// The interface-domain Web module: a pure translation layer between HTTP
/// requests and BesqContext, exactly per interface-domain-design.md §4.
///
/// Assembles all modern controllers (health/status/settings/profiles/algorithm/
/// calculator/fs/logs) onto one `web::Router`, plus the StaticFileServer for
/// `/public` (embedded + optional disk root), plus a session-owned SseHub that
/// the calculator/logs SSE events handlers subscribe to. Errors map to
/// `{ok:false,error}` envelopes with the status codes from the design spec:
/// 400 input / 404 unknown / 409 conflict / 500 internal.
class WebModule {
public:
    explicit WebModule(BesqContext& ctx);
    /// 析构排序不变量：SseHub 必须在 Reactor/controller teardown 之前 drained
    /// （clear()），也必须在 solve worker join 之前 drained。WebModule 析构体先调用
    /// `_impl->_hub.clear()`，再释放 `_impl`（其成员按 `_router` → `_solve` → `_hub`
    /// 逆序析构）——否则连接 on_close 会命中已析构的控制器 `this`，而 `_solve` dtor
    /// join worker 期间的 publish 会命中已析构 Reactor 的帧汇（见 WebModule.cpp）。
    ~WebModule();

    WebModule(const WebModule&) = delete;
    WebModule& operator=(const WebModule&) = delete;

    /// Inject the SPA assets (path → resource), mounted under `/public`.
    void set_static_resources(std::map<std::string, StaticResource> embedded);
    /// Mount a disk root as a `/public` fallback (dev hot-reload); optional.
    void mount_res_dir(std::filesystem::path root);

    /// Dispatch one HTTP request. Never throws; returns a response
    /// (200/307/400/404/405/409/500) with a JSON envelope on errors.
    HttpResponse dispatch(const HttpRequest& req);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace web
