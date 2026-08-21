#include "AppConfig.h"
#include "BuildConfig.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "domain/interface/BesqContext.h"
#include "domain/interface/cli/CLIApp.h"
#include "domain/interface/cli/CtrlInterrupt.h"
#include "domain/interface/components/BuiltinI18n.h"
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/RateLimiter.h"
#include "domain/interface/web/WebModule.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// HTTP API 服务（besq serve 子命令）
//
// 前端已迁出独立项目；本仓库仅保留 HTTP 服务：HttpServer +
// WebModule（8 控制器 /health, /api/* + SSE + /public 静态能力），由同一个
// besq 可执行文件通过 `besq serve` 子命令启动（CLIApp::parse 分派）。
// ============================================================================

namespace {

/// 运行 HTTP API 服务（阻塞至 Ctrl+C / server.stop()）。
/// 参数来自 CLIApp::Config 的 serve 字段（`besq serve` 子命令解析）。
int run_serve(const CLIApp::Config& args) {
    auto& cfg = AppConfig::get();
    // 服务进程覆盖 console 阈值到 Info：启动/关闭/错误进 console 可见；DEBUG
    // 行只进文件（FileConsumer 按文件阈值门控）不刷 console。仅此模式覆盖，
    // CLI 不受影响（CLAUDE.md 的 json/compact 机器输出保护）。
    cfg.log_console_level = 1;
    setup_logger(cfg.logger_config());

    const std::string host    = args.serve_host.empty() ? cfg.http_host : args.serve_host;
    const uint16_t    port    = args.serve_port != 0 ? static_cast<uint16_t>(args.serve_port) : cfg.http_port;
    const size_t      workers = args.serve_workers != 0 ? args.serve_workers : cfg.http_workers;

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
    const std::string res_dir = !args.serve_res_dir.empty() ? args.serve_res_dir : cfg.http_res_dir;
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

    // ── 控制符交互基础设施（^C 优雅中断，spec §3.2 / plan Task 1）──
    // tty 门控：仅 stdin 为交互终端时注册平台 ^C handler；管道/脚本/CI 不注册
    // （^C 走平台默认终止，行为零回归）。handler 内部再按"当前是否求解中"门控
    // （solve_interrupt_gate）：非求解时也不拦截（走默认终止）——serve 路径不
    // 注册 ctx 指针（CtrlInterrupt.h），^C 语义与注册前一致。进程级注册一次，
    // 不注销（进程生命周期）。
    if (cli_ctrl::stdin_is_tty())
        cli_ctrl::register_solve_interrupt_handler();

    // ── Detect target app and route ──
    // 切片 1：CLIParser 子命令体系。serve 走 run_serve；其余（solve/profile/algo）
    // 由 CLIApp::run 统一处理（run 内部再 parse 一次，成本可忽略；测试接口不变）。
    const char* prog = argc > 0 ? argv[0] : "besq";
    CLIApp::Config cfg = CLIApp::parse(argc, argv);
    if (cfg.cmd == CLIApp::Config::Cmd::serve) {
        if (cfg.help) {
            std::cout << CLIApp::help_text(prog, std::vector<std::string_view>{"serve"}) << std::endl;
            return 0;
        }
        if (cfg.version) {
            std::cout << BESQ_PROJECT_NAME << " v" << BESQ_VERSION << std::endl;
            return 0;
        }
        return run_serve(cfg);
    }
    // CLIApp owns its output flush: ~CLIApp() (the temporary is destroyed
    // right after run() returns) flushes std::cout and drains the async
    // Logger queue while this process is still alive.  The exit-time
    // implicit flush was intermittently LOSING buffered output (observed
    // on --list-algorithms: the list printed only ~2/3 of runs) because
    // static-teardown ordering is unreliable in the EXE + SHARED
    // besq-common-log layout — see CLIApp::flush_output().
    return CLIApp().run(argc, argv);

} catch (const std::exception& e) {
    std::cerr << tr_fmt("main.err.error_prefix", e.what()) << std::endl;
    return 1;
}
