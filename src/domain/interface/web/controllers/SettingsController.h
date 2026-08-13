#pragma once
#include "domain/interface/components/http/HttpController.h"
#include <atomic>
#include <mutex>

namespace web {

/// GET/PATCH /api/settings. `lang`, `log_level`, `log_console`,
/// `log_console_level`, `log_retention` are writable at runtime;
/// `gui_host`/`gui_port`/`gui_workers`/`memory_mb`/`sandbox_enabled` and the
/// path fields (`data_dir`/`log_dir`/`algo_dir`) are read-only (set at
/// startup — the server is already bound).  Every successful PATCH persists
/// the five writable fields to <cwd>/config.json (best-effort; the GUI
/// reloads them at startup — see AppConfig).
///
/// `effective_port`（可选）指向实际绑定端口，由 WebModule 在 server.start()
/// 成功后注入（配置 0 = OS 自动分配，真实端口 >0）：GET 的 gui_port 用它
/// 覆盖配置值；nullptr（测试直连 Router 的 TestApp 等）→ 回退配置值。
///
/// Every handler holds _gate FIRST — the same web-layer _ctx_gate that
/// WebSolveService holds for its whole _ctx window. patch() mutates
/// LanguageManager::_active (a global singleton the solve worker reads via
/// tr/tr_fmt) and the Logger singleton; the reactor threads run handlers
/// concurrently, so unlocked access would be a data race.
class SettingsController : public HttpController<SettingsController> {
public:
    using Self = SettingsController;

    explicit SettingsController(std::mutex& gate, std::atomic<uint16_t>* effective_port = nullptr)
        : _gate(gate), _effective_port(effective_port) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/api/settings", get),
            BESQ_ROUTE(Patch, "/api/settings", patch),
        };
    }

    Response get();
    Response patch(const HttpRequest&, const PathParams&, const Json&);

private:
    std::mutex& _gate;
    std::atomic<uint16_t>* _effective_port; // actual bound port (0 = not injected)
};

} // namespace web
