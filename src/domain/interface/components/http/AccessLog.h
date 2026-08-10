#pragma once
#include "HttpCommon.h"
#include "Middleware.h"

namespace web {

/// Combined Log Format 访问日志中间件（INFO 级，经全局异步 Logger）：
///   ip - - [dd/Mon/yyyy:HH:mm:ss +zzzz] "METHOD path HTTP/1.1" status bytes "Referer" "UA"
/// 字段对齐 nginx/Apache 惯例；客户端可控字段做日志注入消毒（控制字符→'_'）；
/// is_stream → bytes 记 "-"；next() 异常时记 500 后重抛（防御性）。
/// 客户端 IP 经 client_addr(req, policy) 解析（可信代理 XFF 策略）。
class AccessLogger {
public:
    explicit AccessLogger(ClientAddrPolicy policy) : _policy(std::move(policy)) {}

    HttpResponse operator()(const HttpRequest& req, const Next& next);

private:
    void log_line(const HttpRequest& req, const HttpResponse& resp);
    std::string request_line(const HttpRequest& req);
    ClientAddrPolicy _policy;
};

Middleware make_access_logger(ClientAddrPolicy policy = {});

} // namespace web
