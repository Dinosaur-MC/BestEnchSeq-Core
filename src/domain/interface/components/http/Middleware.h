#pragma once
#include "HttpCommon.h"
#include <functional>
#include <string>
#include <vector>

namespace web {

/// middleware 链类型（Express/Koa 风格）：m(req, next) 调用 next(req) 继续链，
/// 不调用即短路（限流 429 路径）。链在 HttpServer::run() 启动时组装；
/// use() 与 set_handler 同约束：run 前注册（run 后注册对已运行服务器无效）。
using Next = std::function<HttpResponse(const HttpRequest&)>;
using Middleware = std::function<HttpResponse(const HttpRequest&, const Next&)>;

/// 客户端 IP 解析策略（限流 key 与访问日志客户端 IP 共用，见 client_addr）。
struct ClientAddrPolicy {
    /// true = 直连对端 ∈ trusted_proxies 时采信 X-Forwarded-For 最右条目
    /// （Nginx 前置部署）；false（默认）= XFF 完全忽略（防伪造头绕过限流）。
    bool trust_forwarded = false;
    std::vector<std::string> trusted_proxies = {"127.0.0.1"};
};

/// 真实客户端 IP：见 ClientAddrPolicy。req.remote_addr 为空 → 返回空串。
std::string client_addr(const HttpRequest& req, const ClientAddrPolicy& policy);

} // namespace web
