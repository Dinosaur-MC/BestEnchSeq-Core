#include "AppConfig.h"
#include "BuildConfig.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "domain/interface/BesqContext.h"
#include "domain/interface/cli/CLIApp.h"
#include "domain/interface/components/BuiltinI18n.h"
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/RateLimiter.h"
#include "domain/interface/web/WebModule.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

// ============================================================================
// HTTP API 服务（--api serve）
//
// 前端已迁出独立项目；本仓库仅保留 HTTP 服务：HttpServer +
// WebModule（8 控制器 /health, /api/* + SSE + /public 静态能力），由同一个
// besq 可执行文件通过 `besq --api serve` 启动（CLIApp::detect_target 路由）。
// ============================================================================

namespace {

/// `besq --api serve` 的命令行参数（覆盖 AppConfig 的 BESQ_HTTP_* 环境变量）。
struct ServeArgs {
    std::string host;       // --host     (默认 BESQ_HTTP_HOST / 127.0.0.1)
    uint16_t    port = 0;   // --port     (0 = 沿用配置，即 BESQ_HTTP_PORT / 0 = OS 自动分配)
    size_t      workers = 0;// --workers  (0 = 沿用配置，即 BESQ_HTTP_WORKERS / 2)
    std::string res_dir;    // --res-dir  (可选 /public 磁盘根；默认 BESQ_HTTP_RES_DIR，不挂载)
    bool        help = false;
    bool        version = false;
};

/// 解析 `--api serve` 后的参数。非法数值（非数字/越界）回退默认并 LOG_WARN——
/// 与 AppConfig 对 config.json 坏字段的逐字段容错策略一致。
ServeArgs parse_serve_args(int argc, char* argv[]) {
    ServeArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--api" && i + 1 < argc) { ++i; continue; } // 跳过目标选择对
        if (arg == "--help" || arg == "-h") { a.help = true; continue; }
        if (arg == "--version" || arg == "-V") { a.version = true; continue; }
        auto take = [&](std::string& out) {
            if (i + 1 < argc) { out = argv[++i]; return true; }
            return false;
        };
        if (arg == "--host") {
            if (!take(a.host))
                LOG_WARN("--host requires a value; ignored");
        } else if (arg == "--port") {
            std::string v;
            if (take(v)) {
                try {
                    const unsigned long p = std::stoul(v);
                    a.port = p <= 65535 ? static_cast<uint16_t>(p) : 0;
                    if (p > 65535)
                        LOG_WARN("--port %s out of range (0..65535); using default", v.c_str());
                } catch (...) {
                    LOG_WARN("--port %s is not a number; using default", v.c_str());
                }
            } else {
                LOG_WARN("--port requires a value; ignored");
            }
        } else if (arg == "--workers") {
            std::string v;
            if (take(v)) {
                try {
                    const long w = std::stol(v);
                    a.workers = w > 0 ? static_cast<size_t>(w) : 0;
                    if (w <= 0)
                        LOG_WARN("--workers %s must be >= 1; using default", v.c_str());
                } catch (...) {
                    LOG_WARN("--workers %s is not a number; using default", v.c_str());
                }
            } else {
                LOG_WARN("--workers requires a value; ignored");
            }
        } else if (arg == "--res-dir") {
            if (!take(a.res_dir))
                LOG_WARN("--res-dir requires a value; ignored");
        }
        // 其余参数忽略（CLI 专属选项在 serve 模式下无意义）。
    }
    return a;
}

void print_serve_help() {
    std::cout
        << "besq --api serve — BestEnchSeq HTTP API service (REST + SSE)\n"
        << "The frontend lives in a separate project; this mode exposes\n"
        << "the HTTP service only (/health, /api/*, SSE events, /public static).\n"
        << "Usage: besq --api serve [--host ADDR] [--port PORT] [--workers N] [--res-dir DIR]\n"
        << "  --host ADDR         bind address (default: BESQ_HTTP_HOST / 127.0.0.1)\n"
        << "  --port PORT         bind port; 0 = OS auto-assign (default: BESQ_HTTP_PORT / 0)\n"
        << "  --workers N         HTTP consumer threads (default: BESQ_HTTP_WORKERS / 2)\n"
        << "  --res-dir DIR       optional /public disk root (default: BESQ_HTTP_RES_DIR, none)\n"
        << "Environment: BESQ_HTTP_HOST, BESQ_HTTP_PORT, BESQ_HTTP_WORKERS, BESQ_HTTP_RES_DIR\n"
        << "             BESQ_LANG (language; config.json lang otherwise)\n"
        << "Runtime settings (lang/log_level/log_console/log_console_level) are\n"
        << "persisted to <exe_dir>/config.json by PATCH /api/settings and reloaded\n"
        << "at startup (env vars still win: env > config.json > default)\n";
}

/// 运行 HTTP API 服务（阻塞至 Ctrl+C / server.stop()）。
int run_serve(const ServeArgs& args) {
    auto& cfg = AppConfig::get();
    // 服务进程覆盖 console 阈值到 Info：启动/关闭/错误进 console 可见；DEBUG
    // 行只进文件（FileConsumer 按文件阈值门控）不刷 console。仅此模式覆盖，
    // CLI 不受影响（CLAUDE.md 的 json/compact 机器输出保护）。
    cfg.log_console_level = 1;
    setup_logger(cfg.logger_config());

    const std::string host    = args.host.empty() ? cfg.http_host : args.host;
    const uint16_t    port    = args.port != 0 ? args.port : cfg.http_port;
    const size_t      workers = args.workers != 0 ? args.workers : cfg.http_workers;

    BesqContext ctx;
    // 领域通用自动加载：内建数据 → profiles → 算法插件 → 磁盘语言
    // （AutoLoadPipeline；默认目录均基于 exe 目录）。
    ctx.auto_load();

    // WebModule 做全部路由（/health, /api/*, /public 静态 + SSE），HTTP 服务器
    // 只负责字节 ↔ dispatch 桥接。
    //
    // 关机顺序依赖：server 先声明 → 逆序析构时 module 先死，其 hub.clear() 先于
    // Reactor（server 所有）释放执行。SseHub 订阅共享持有 Connection，帧汇捕获裸
    // Reactor*；若 Reactor 先于 hub.clear() 释放，任何日志/任务帧 publish 都会调用
    // 已析构 Reactor 的 loop → UAF。hub 订阅的生命周期必须短于 Reactor。
    web::HttpServer server;

    web::WebModule module(ctx);
    // /public 磁盘根（可选）。前端已迁出独立项目，静态服务能力保留——后续可
    // 按需作为 profiles 直读等静态资源访问接口。仅响应显式 --res-dir /
    // BESQ_HTTP_RES_DIR，默认不挂载。
    const std::string res_dir = !args.res_dir.empty() ? args.res_dir : cfg.http_res_dir;
    if (!res_dir.empty() && std::filesystem::is_directory(res_dir))
        module.mount_res_dir(res_dir);

    // fallback 仅在 server 存活期内被调用；module 声明在后（后析构），引用始终有效。
    server.set_fallback([&](const web::HttpRequest& r) { return module.dispatch(r); });

    // 限流（默认关闭；HTTP 服务显式开启，本地宽松阈值）。部署经 Nginx 前置时：
    // rl.client_addr_policy.trust_forwarded = true;（对端恒为 nginx）
    web::RateLimitConfig rl;
    rl.enabled = true;
    server.use(web::make_rate_limiter(rl));
    // 访问日志默认开启（Combined 格式，INFO 级，见 AccessLog.h）。

    if (!server.start(host, port, workers)) {
        LOG_ERROR("failed to bind %s:%u", host.c_str(), static_cast<unsigned>(port));
        std::cerr << "besq: failed to bind " << host << ":" << port << "\n";
        return 1;
    }

    // 回填实际绑定端口：配置 0 = OS 自动分配，HttpServer::port() 拿到真实端口
    // 后注入 WebModule——GET /api/settings 的 http_port 显示它。
    module.set_effective_port(server.port());

    LOG_INFO("http service listening at http://%s:%u/", host.c_str(), static_cast<unsigned>(server.port()));

    server.run();
    LOG_INFO("http service shutting down");
    besq::log::flush();
    return 0;
}

} // namespace

int main(int argc, char* argv[]) try {
    // ── Configuration ──
    auto& app_cfg = AppConfig::get();  // global singleton — consumers read it directly

    // ── i18n setup ──
    register_builtin_translations(LanguageManager::instance());
    // On-demand language file directory (AppConfig: <exe_dir>/langs default,
    // BESQ_LANG_DIR overrides).
    try {
        if (!app_cfg.langs_dir.empty())
            LanguageManager::instance().set_langs_dir(app_cfg.langs_dir);
    } catch (...) {}
    CLIApp::apply_lang(argc, argv);

    // ── Logger setup ──
    setup_logger(app_cfg.logger_config());

    // ── Detect target app and route ──
    auto target = CLIApp::detect_target(argc, argv);

    if (target == "cli") {
        // CLIApp owns its output flush: ~CLIApp() (the temporary is destroyed
        // right after run() returns) flushes std::cout and drains the async
        // Logger queue while this process is still alive.  The exit-time
        // implicit flush was intermittently LOSING buffered output (observed
        // on --list-algorithms: the list printed only ~2/3 of runs) because
        // static-teardown ordering is unreliable in the EXE + SHARED
        // besq-common-log layout — see CLIApp::flush_output().
        return CLIApp().run(argc, argv);
    }

    if (target == "serve") {
        const ServeArgs args = parse_serve_args(argc, argv);
        if (args.help) {
            print_serve_help();
            return 0;
        }
        if (args.version) {
            std::cout << "besq " << BESQ_VERSION << "\n";
            return 0;
        }
        return run_serve(args);
    }

    std::cerr << "Unknown API target: " << target << "\n";
    return 1;

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
