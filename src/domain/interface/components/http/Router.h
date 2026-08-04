// src/domain/interface/components/http/Router.h
#pragma once
#include "HttpCommon.h"
#include "RouteDef.h"
#include <concepts>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace web {

/// 控制器抛出的带状态码错误：Router::dispatch 捕获并映射为错误 envelope。
class WebHttpError : public std::runtime_error {
public:
    WebHttpError(int status, std::string code, std::string message)
        : std::runtime_error(std::move(message)), status(status), code(std::move(code)) {}
    int status;
    std::string code;
};

/// 控制器抽象基类：暴露其 constexpr 路由表。
class HttpControllerBase {
public:
    virtual ~HttpControllerBase() = default;
    virtual std::span<const ConstRouteDef> routes() const = 0;
};

template <typename T>
concept Controller = std::derived_from<T, HttpControllerBase>;

namespace detail {
/// pattern 拆段（跳过首 '/'），逐段访问器（不物化 vector，constexpr 友好）。
constexpr bool is_param_seg(std::string_view seg) {
    return seg.size() >= 3 && seg.front() == '{' && seg.back() == '}';
}
/// 从 pattern 取第 k 段（0-based，跳过首 '/'）；越界返回 false。
constexpr bool segment_at(std::string_view p, size_t k, std::string_view& out) {
    size_t pos = 1; // skip '/'
    for (size_t i = 0; i < k; ++i) {
        auto nl = p.find('/', pos);
        if (nl == std::string_view::npos) return false;
        pos = nl + 1;
    }
    auto end = p.find('/', pos);
    out = p.substr(pos, end == std::string_view::npos ? p.size() - pos : end - pos);
    return true;
}
constexpr size_t segment_count(std::string_view p) {
    size_t n = 0; std::string_view s;
    while (segment_at(p, n, s)) ++n;
    return n;
}
} // namespace detail

/// 编译期路由校验：格式 / 重复 / 同方法同父级同段位参数与固定不混层。
consteval bool validate_routes(std::span<const ConstRouteDef> routes) {
    // ① 格式
    for (const auto& r : routes) {
        auto p = r.pattern;
        if (p.empty() || p.front() != '/' || p.back() == '/') return false;
        bool in_param = false;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '{') { if (in_param) return false; in_param = true; }
            else if (p[i] == '}') {
                if (!in_param) return false;
                in_param = false;
                if (i == 0 || p[i - 1] == '{') return false;   // empty param name
            }
            else if (in_param && (p[i] == '/' || p[i] == ' ')) return false;
        }
        if (in_param) return false;
    }
    // ② 重复 + ③ 混层（同方法，前缀相同处出现 参数/固定 分歧）
    for (size_t i = 0; i < routes.size(); ++i)
        for (size_t j = i + 1; j < routes.size(); ++j) {
            const auto& a = routes[i];
            const auto& b = routes[j];
            if (a.method != b.method) continue;          // 跨方法不参与重复/混层判定
            if (a.pattern == b.pattern) return false;    // 同方法同 pattern = 重复
            size_t na = detail::segment_count(a.pattern);
            size_t nb = detail::segment_count(b.pattern);
            size_t n = na < nb ? na : nb;
            bool prefix_eq = true;
            for (size_t k = 0; k < n; ++k) {
                std::string_view sa, sb;
                detail::segment_at(a.pattern, k, sa);
                detail::segment_at(b.pattern, k, sb);
                if (prefix_eq && (detail::is_param_seg(sa) != detail::is_param_seg(sb))) return false;
                if (sa != sb) prefix_eq = false;
            }
        }
    return true;
}

class Router {
public:
    /// 注册一个控制器（模板，自动聚合其 constexpr 路由表）。
    template <Controller C, typename... Args>
    Router& register_controller(Args&&... args) {
        _controllers.push_back(std::make_unique<C>(std::forward<Args>(args)...));
        return *this;
    }

    /// 分发；方法错→405+Allow，路径未知→404，WebHttpError→对应状态，JsonException→400，其他→500。
    HttpResponse dispatch(const HttpRequest& req);

private:
    static bool match_segments(std::string_view pattern, std::string_view path, PathParams& out);
    std::vector<std::unique_ptr<HttpControllerBase>> _controllers;
};

} // namespace web
