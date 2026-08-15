#include "AppConfig.h"
#include "BuildConfig.h"
#include "builtin/FrontendAssets.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "common/utils/ExeDir.hpp"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/BuiltinI18n.h"
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/RateLimiter.h"
#include "domain/interface/web/WebModule.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace besq::data {
// Embedded frontend assets are accessed through the generated uniform
// interface: besq::data::raw(FrontendAsset) — see builtin/FrontendAssets.h.
// The ResourceId members used below are declared (generated) in
// builtin/EmbeddedResources_generated.h.
} // namespace besq::data

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// Served frontend assets: URL → content type + ResourceId.
/// The ResourceId members MUST match the member names declared in the
/// frontend group of besq_embed_resources() in CMakeLists.txt; the generator
/// rejects unknown/duplicate members at configure time.
struct Asset {
    const char* url;
    const char* type;
    besq::data::ResourceId id;
};
static constexpr Asset kAssets[] = {
    {"/index.html", "text/html", besq::data::ResourceId::frontend_index_html},
    {"/styles.css", "text/css", besq::data::ResourceId::frontend_styles_css},
    {"/app.js", "text/javascript", besq::data::ResourceId::frontend_app_js},
    {"/api.js", "text/javascript", besq::data::ResourceId::frontend_api_js},
    {"/i18n.js", "text/javascript", besq::data::ResourceId::frontend_i18n_js},
    {"/names_zh.js", "text/javascript", besq::data::ResourceId::frontend_names_zh_js},
    {"/sprite.js", "text/javascript", besq::data::ResourceId::frontend_sprite_js},
    {"/vendor/icons/sprite.png", "image/png", besq::data::ResourceId::frontend_vendor_icons_sprite_png},
    {"/vendor/mdui/mdui.css", "text/css", besq::data::ResourceId::frontend_vendor_mdui_mdui_css},
    {"/vendor/mdui/mdui.global.js", "text/javascript", besq::data::ResourceId::frontend_vendor_mdui_mdui_global_js},
    {"/views/calculator.js", "text/javascript", besq::data::ResourceId::frontend_view_calculator},
    {"/views/profiles.js", "text/javascript", besq::data::ResourceId::frontend_view_profiles},
    {"/views/algorithms.js", "text/javascript", besq::data::ResourceId::frontend_view_algorithms},
    {"/views/history.js", "text/javascript", besq::data::ResourceId::frontend_view_history},
    {"/views/settings.js", "text/javascript", besq::data::ResourceId::frontend_view_settings},
    {"/views/status.js", "text/javascript", besq::data::ResourceId::frontend_view_status},
    {"/views/pager.js", "text/javascript", besq::data::ResourceId::frontend_view_pager},
};

/// Build the static-resource table. When `frontend_dir` is non-empty (dev
/// mode --frontend-dir), read files from disk for hot reload; otherwise use
/// the embedded copies.
static std::map<std::string, web::StaticResource> build_static(const std::string& frontend_dir) {
    std::map<std::string, web::StaticResource> m;
    for (const auto& a : kAssets) {
        if (!frontend_dir.empty()) {
            auto disk = std::filesystem::path(frontend_dir) / std::string(a.url).substr(1);
            if (std::filesystem::exists(disk)) {
                m[a.url] = web::StaticResource{a.type, read_file(disk.string())};
                continue;
            }
        }
        m[a.url] = web::StaticResource{a.type, std::string(besq::data::raw(a.id))};
    }
    return m;
}

/// /public 磁盘根（可选，部署自定义）。仅响应显式 BESQ_GUI_RES_DIR——前端
/// 静态资源全部编译期嵌入，默认零磁盘读取（无自动检测项；--frontend-dir
/// 是 dev 热重载的独立通道）。
static std::filesystem::path resolve_res_dir(const std::string& res_dir) {
    if (!res_dir.empty() && std::filesystem::is_directory(res_dir))
        return std::filesystem::path(res_dir);
    return {};
}

int main(int argc, char* argv[]) try {
    auto& cfg = AppConfig::get();
    register_builtin_translations(LanguageManager::instance());
    // 持久化语言（config.json / BESQ_LANG，env > config > 默认）在 PATCH 之外
    // 启动即生效；未知 code 由 select() 回退 en_US。
    if (!cfg.runtime_lang.empty())
        LanguageManager::instance().select(cfg.runtime_lang);
    // GUI 进程覆盖 console 阈值到 Info：启动/关闭/错误进 console 可见；DEBUG
    // 行只进文件（FileConsumer 按文件阈值门控）不刷 console。仅此进程覆盖，
    // CLI 不受影响（CLAUDE.md 的 json/compact 机器输出保护）。
    cfg.log_console_level = 1;
    setup_logger(cfg.logger_config());

    bool open_browser = cfg.gui_open_browser;
    std::string frontend_dir; // --frontend-dir (dev hot reload)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::cout << "besq-gui — BestEnchSeq Web GUI\n"
                      << "Usage: besq-gui [--browser] [--frontend-dir DIR]\n"
                      << "  --browser           open the default browser (the v1 host)\n"
                      << "  --frontend-dir DIR  serve the SPA from DIR (dev hot-reload)\n"
                      << "Environment: BESQ_GUI_HOST, BESQ_GUI_PORT, BESQ_GUI_OPEN_BROWSER\n"
                      << "             BESQ_GUI_WORKERS (consumer threads, default 2)\n"
                      << "             BESQ_GUI_RES_DIR (optional /public disk root)\n"
                      << "             BESQ_LANG (language; config.json lang otherwise)\n"
                      << "Runtime settings (lang/log_level/log_console/log_console_level) are\n"
                      << "persisted to <exe_dir>/config.json by PATCH /api/settings and reloaded\n"
                      << "at startup (env vars still win: env > config.json > default)\n";
            return 0;
        }
        if (a == "--version" || a == "-V") {
            std::cout << "besq-gui " << BESQ_VERSION << "\n";
            return 0;
        }
        if (a == "--browser")
            open_browser = true;
        else if (a == "--frontend-dir" && i + 1 < argc)
            frontend_dir = argv[++i];
    }

    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    // WebModule 做全部路由（/health, /api/*, /public 静态 + SSE），HTTP 服务器只负责
    // 字节 ↔ dispatch 桥接。dispatch 结果原样返回，避免 HttpResponse::json 重包装把
    // SPA 的 text/html 资产强制成 application/json。
    //
    // 关机顺序依赖：server 先声明 → 逆序析构时 module 先死，其 hub.clear() 先于
    // Reactor（server 所有）释放执行。SseHub 订阅共享持有 Connection，帧汇捕获裸
    // Reactor*；若 Reactor 先于 hub.clear() 释放，任何日志/任务帧 publish 都会调用
    // 已析构 Reactor 的 loop → UAF。hub 订阅的生命周期必须短于 Reactor。
    web::HttpServer server;

    web::WebModule module(ctx);
    module.set_static_resources(build_static(frontend_dir));
    auto res_dir = resolve_res_dir(cfg.gui_res_dir);
    if (!res_dir.empty())
        module.mount_res_dir(res_dir);

    // fallback 仅在 server 存活期内被调用；module 声明在后（后析构），引用始终有效。
    server.set_fallback([&](const web::HttpRequest& r) { return module.dispatch(r); });

    // 限流（默认关闭；GUI 显式开启，本地宽松阈值）。部署经 Nginx 前置时：
    // rl.client_addr_policy.trust_forwarded = true;（对端恒为 nginx）
    web::RateLimitConfig rl;
    rl.enabled = true;
    server.use(web::make_rate_limiter(rl));
    // 访问日志默认开启（Combined 格式，INFO 级，见 AccessLog.h）。

    if (!server.start(cfg.gui_host, cfg.gui_port, cfg.gui_workers)) {
        LOG_ERROR("failed to bind %s:%u", cfg.gui_host.c_str(), static_cast<unsigned>(cfg.gui_port));
        std::cerr << "besq-gui: failed to bind " << cfg.gui_host << ":" << cfg.gui_port << "\n";
        return 1;
    }

    // 回填实际绑定端口：配置 0 = OS 自动分配，HttpServer::port() 拿到真实端口
    // 后注入 WebModule——GET /api/settings 的 gui_port 显示它（设置页）。
    module.set_effective_port(server.port());

    const auto url = "http://" + cfg.gui_host + ":" + std::to_string(server.port()) + "/";
    LOG_INFO("besq-gui listening at %s", url.c_str());
    std::cout << "besq-gui serving at " << url << "\n";

    // v1 host: the default browser. A native WebView2 window (src/gui/
    // webview_host.*) is future work — it needs the Microsoft WebView2 SDK,
    // which is not vendored. `server.run()` keeps serving on this thread for
    // the process lifetime; Ctrl+C exits.
    if (open_browser) {
#ifdef _WIN32
        std::string cmd = "start \"\" \"" + url + "\"";
        std::system(cmd.c_str());
#else
        std::string cmd = "xdg-open " + url + " >/dev/null 2>&1 &";
        std::system(cmd.c_str());
#endif
    }

    server.run();
    LOG_INFO("server shutting down");
    besq::log::flush();
    return 0;

} catch (const std::exception& e) {
    std::cerr << "besq-gui: " << e.what() << std::endl;
    return 1;
}
