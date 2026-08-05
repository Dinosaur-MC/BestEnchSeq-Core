// src/domain/interface/components/http/HttpController.h
#pragma once
#include "Router.h"
#include <array> // IWYU pragma: keep

namespace web {

/// CRTP 基类：routes() 虚接口暴露派生类的 constexpr 路由表，并在编译期执行校验。
template <typename Derived> class HttpController : public HttpControllerBase {
  public:
    std::span<const ConstRouteDef> routes() const override {
        static constexpr auto defs = [] {
            constexpr auto d = Derived::route_defs();
            static_assert(validate_routes(d), "controller route table invalid (format/duplicate/level-mix)");
            return d;
        }();
        return defs;
    }
};

} // namespace web
