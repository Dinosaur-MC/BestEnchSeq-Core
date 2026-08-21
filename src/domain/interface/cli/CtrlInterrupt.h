#pragma once

// ============================================================================
// CtrlInterrupt — 控制符交互基础设施（spec §3.2 / plan Task 1 + Task 2 集成）
//
// 控制循环架构（用户裁决扩展）：平台 ^C handler 与 stdin 控制字符读取**只置
// 原子请求标志，绝不调用 BesqContext**；实际 ctx 调用（abort_solve /
// pause_solve / resume_solve / save_solve_state）全部由控制循环（CLIApp 主
// 线程，Task 2）每 ~200ms 周期 exchange 消费执行——POSIX signal handler
// 因此严格 async-signal-safe（纯原子存储 + 纯原子 bool 门控 load），
// Windows/POSIX 行为一致。
//
// 控制符集（用户裁决）：^C(\x03)=Abort、^P(\x10)=Pause、^R(\x12)=Resume、
// ^S(\x13)=Save（仅 Paused 有效）。
//   - ^C：Windows 控制台事件（SetConsoleCtrlHandler）/ POSIX 信号（SIGINT）
//     送达，不出现于 stdin 数据流（ENABLE_PROCESSED_INPUT / ISIG 消费）；
//     \x03 映射为安全网（若某平台将 ^C 作为字节送达）。
//   - ^P/^R/^S：非信号、非控制台事件——经 stdin 控制字符到达
//     （try_read_stdin_char 非阻塞读取；依赖 StdinCtrlGuard 非规范模式逐键
//     到达，规范模式按行缓冲、Enter 才送达）。
//
// 组件：
//   1. SolveControlRequest 枚举 + g_solve_control 原子（handler/读键置位，
//      控制循环 exchange 消费）。
//   2. g_solve_interrupted 粘性标志（Abort 请求置位时同步置位，供 Task 2 在
//      solve 返回后输出中断摘要；reset_solve_interrupted() 复位）。
//   3. stdin_is_tty() tty 门控（测试钩子 set_stdin_tty_override 可强制 true）
//      + g_solve_active 求解活跃标志（handler 门控读它——纯原子 load，严格
//      async-signal-safe；T1 review N1 门控修正，替代旧 solve_progress 读取）。
//   4. register_solve_interrupt_handler()：平台 ^C handler（进程级一次，
//      main 启动、tty 时；不注销），带求解门控（非求解 → 默认终止）。
//   5. try_read_stdin_char() 非阻塞单字符读取。
//   6. StdinCtrlGuard：RAII 终端原始化（POSIX 清 ICANON|IXON|ECHO——IXON
//      禁用防 ^S 流控吞键；Windows 清 ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT），
//      控制循环（Task 2）包住求解期间、析构恢复——REPL 等行输入消费方保持
//      规范模式，零影响。
//   7. decide_solve_control() 状态机裁决（纯函数，Task 2 控制循环消费；
//      可单测——spec §3.3 + T1 review N5 粘性中断优先）。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

class BesqContext;

namespace cli_ctrl {

/// 求解控制请求（控制循环消费；handler/读键只置位不执行——约束 3 / spec §3.2）。
enum class SolveControlRequest : uint8_t { None, Abort, Pause, Resume, Save };

/// 控制请求标志（进程级单实例）：^C/^P/^R/^S 置位，控制循环（Task 2）每
/// ~200ms exchange(None) 消费并按状态机执行对应 ctx 调用
/// （Abort→abort_solve / Pause→pause_solve / Resume→resume_solve /
/// Save→save_solve_state，仅 Paused 有效）。
inline std::atomic<SolveControlRequest> g_solve_control{SolveControlRequest::None};

/// "本次求解被中断（Abort 被请求）"粘性标志：Abort 请求置位时同步置位，供
/// Task 2 在 solve 返回后输出中断摘要（即使控制循环与求解结束竞态也未丢失）；
/// reset_solve_interrupted() 复位。
inline std::atomic<bool> g_solve_interrupted{false};

/// 求解活跃标志（T1 review N1 门控修正，binding）：tty 控制循环（CLIApp
/// run_solve_control_loop）在启动 solver 线程前置位、join 后清位；平台
/// handler 的求解门控读它（纯原子 bool load——严格 async-signal-safe，
/// 替代旧 solve_progress() 门控的原子 shared_ptr load，后者内部有锁）。
/// 非 tty / serve 恒 false → handler 门控不通过 → ^C 默认终止，行为不变。
inline std::atomic<bool> g_solve_active{false};

/// stdin 是否为交互终端（tty 门控）：仅 tty 注册 ^C handler + 控制循环读键。
/// 非 tty（管道/脚本/CI）不注册——^C 走平台默认终止，输出零回归。
/// 测试钩子 g_stdin_tty_override 可强制 true（控制循环路径进程内单测；
/// 生产代码不调用）。
inline std::atomic<bool> g_stdin_tty_override{false};

/// 测试钩子：强制 stdin_is_tty() 返回 \p v（默认 false——测试环境 stdin 为
/// 重定向文件）。仅供 tests 使用，生产代码不调用。
inline void set_stdin_tty_override(bool v) noexcept { g_stdin_tty_override.store(v); }

inline bool stdin_is_tty() noexcept {
    if (g_stdin_tty_override.load())
        return true;
#ifdef _WIN32
    return ::_isatty(::_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

/// 复位中断状态（Task 2 消费后 / 下一轮求解前调用）：清粘性中断标志 + 挂起
/// 请求（控制循环运行期间由循环自行 exchange 消费，此复位仅用于求解间隙）。
inline void reset_solve_interrupted() noexcept {
    g_solve_control.store(SolveControlRequest::None);
    g_solve_interrupted.store(false);
}

/// 求解门控：仅"当前有求解在运行"时 ^C handler 才消费 Abort（置请求，进程
/// 存活）；非求解 → 走默认终止。T1 review N1（binding）：判定 = 纯原子活跃
/// 标志 g_solve_active（tty 控制循环置位/清位），**不再读 solve_progress()**
/// （原子 shared_ptr load 内部有锁，非严格信号安全）。\p ctx 参数保留（历史
/// 调用/测试兼容），门控判定不依赖它。实现于 CtrlInterrupt.cpp。
bool solve_interrupt_gate(BesqContext* ctx) noexcept;

/// 状态机动作（控制循环执行体；spec §3.3）。
enum class SolveControlAction : uint8_t { None, Abort, Pause, Resume, Save };

/// 状态机裁决（纯函数，可单测；spec §3.3 + T1 review N5 lost-interrupt 优先）：
/// - 粘性中断标志置位 → 恒 Abort（即使槽位持非 Abort 请求——读键存储
///   ^P/^R/^S 落在 handler Abort 之后、循环 exchange 之前的单槽
///   last-write-wins 竞态下，中断事实不丢）。
/// - Running（\p paused=false）：Abort→Abort、Pause→Pause、Resume/Save→忽略。
/// - Paused（\p paused=true）：Resume→Resume、Save→Save、Abort→Abort、
///   Pause→忽略。
inline SolveControlAction decide_solve_control(SolveControlRequest req,
                                               bool sticky_interrupted,
                                               bool paused) noexcept {
    if (sticky_interrupted)
        return SolveControlAction::Abort;
    switch (req) {
        case SolveControlRequest::Abort:
            return SolveControlAction::Abort;
        case SolveControlRequest::Pause:
            return paused ? SolveControlAction::None : SolveControlAction::Pause;
        case SolveControlRequest::Resume:
            return paused ? SolveControlAction::Resume : SolveControlAction::None;
        case SolveControlRequest::Save:
            return paused ? SolveControlAction::Save : SolveControlAction::None;
        case SolveControlRequest::None:
            return SolveControlAction::None;
    }
    return SolveControlAction::None;
}

/// 注册平台 ^C handler（进程级一次；main 启动、stdin_is_tty() 时调用；不注销）：
/// - Windows SetConsoleCtrlHandler：^C 且求解中 → 置 Abort 请求 + 中断标志，
///   返回 TRUE（已处理）；非 ^C 事件 / 非求解 → FALSE（默认终止）。
/// - POSIX sigaction(SIGINT)：求解中 → 置 Abort 请求 + 中断标志（信号被消费，
///   进程存活——abort_solve 由控制循环执行）；非求解 → 恢复默认处置 + 重发
///   SIGINT（进程按默认语义终止）。
/// handler 只置原子标志，绝不调用 BesqContext（约束 3 / spec §3.2）。
void register_solve_interrupt_handler() noexcept;

/// 非阻塞读 stdin 单字符（控制循环 Task 2 捕获 ^P/^R/^S）：有可读字符 → true
/// （\p out 填原始字节）；无 → false（不阻塞）。
/// POSIX：poll(STDIN_FILENO, 0 超时) + read 1 字节；Windows：_kbhit() +
/// _getch()（控制台专用；stdin 重定向时恒 false）。依赖 StdinCtrlGuard 非规范
/// 模式才能逐键到达（规范模式按行缓冲、Enter 才送达）。
bool try_read_stdin_char(char& out) noexcept;

/// RAII 终端原始化守卫（控制循环 Task 2 包住求解期间；stdin 非 tty 时为 no-op）：
/// - POSIX：保存 termios → 清 ICANON（非规范，逐键到达）| IXON（禁流控——
///   ^S 不被吞）| ECHO（不回显）→ 析构恢复。**保留 ISIG**：^C 仍走 SIGINT。
/// - Windows：保存控制台模式 → 清 ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT →
///   析构恢复。**保留 ENABLE_PROCESSED_INPUT**：^C 仍生成 CTRL_C_EVENT。
/// 求解结束后恢复（析构），REPL 等行输入消费方保持规范模式——零影响。
class StdinCtrlGuard {
public:
    StdinCtrlGuard() noexcept;
    ~StdinCtrlGuard();
    StdinCtrlGuard(const StdinCtrlGuard&) = delete;
    StdinCtrlGuard& operator=(const StdinCtrlGuard&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace cli_ctrl
