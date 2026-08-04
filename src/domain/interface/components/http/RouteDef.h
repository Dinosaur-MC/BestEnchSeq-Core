// src/domain/interface/components/http/RouteDef.h
#pragma once
#include "HttpCommon.h"
#include "common/io/json.h"
#include <array>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace web {

using Response = HttpResponse;   // 路由签名/表使用的简写别名

/// 一条端点的编译期元数据。handler 为类型擦除函数指针（void* = 控制器实例）。
struct ConstRouteDef {
    Method method;
    std::string_view pattern;
    Response (*invoke)(void*, const HttpRequest&, const PathParams&);
};

namespace detail {
template <typename...> inline constexpr bool always_false = false;

/// body 解析失败 → 由调用方捕获转 400；缺 body 当作 {}。
inline const Json& parse_body(const HttpRequest& req, Json& stash) {
    try { stash = Json::parse(req.body.empty() ? "{}" : req.body); }
    catch (const JsonException&) { throw std::runtime_error("invalid body"); }
    return stash;
}
} // namespace detail

/// 成员函数指针 NTTP → 静态调用函数。Args 支持 5 种签名，其余 static_assert 拒绝。
template <auto M> struct handler_traits;

template <typename R, typename C, typename... Args, R (C::*M)(Args...)>
struct handler_traits<M> {
    static Response call(void* self, const HttpRequest& req, const PathParams& pp) {
        auto& c = *static_cast<C*>(self);
        return call_impl(c, req, pp, M, std::type_identity<std::tuple<Args...>>{});
    }
private:
    template <typename Tuple>
    static Response call_impl(C& c, const HttpRequest& req, const PathParams& pp, R (C::*m)(Args...), std::type_identity<Tuple>) {
        if constexpr (sizeof...(Args) == 0) {
            return (c.*m)();
        } else if constexpr (std::is_same_v<Tuple, std::tuple<const HttpRequest&>>) {
            return (c.*m)(req);
        } else if constexpr (std::is_same_v<Tuple, std::tuple<const HttpRequest&, const PathParams&>>) {
            return (c.*m)(req, pp);
        } else if constexpr (std::is_same_v<Tuple, std::tuple<const HttpRequest&, const Json&>>) {
            Json stash;
            return (c.*m)(req, detail::parse_body(req, stash));
        } else if constexpr (std::is_same_v<Tuple, std::tuple<const HttpRequest&, const PathParams&, const Json&>>) {
            Json stash;
            return (c.*m)(req, pp, detail::parse_body(req, stash));
        } else {
            static_assert(detail::always_false<Tuple>, "unsupported handler signature: use (), (Request), (Request,PathParams), (Request,Json), (Request,PathParams,Json)");
        }
    }
};

#define BESQ_ROUTE(M, P, H) \
    ::web::ConstRouteDef{ ::web::Method::M, P, &::web::handler_traits<&Self::H>::call }

} // namespace web
