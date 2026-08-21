#include "domain/interface/cli/CtrlInterrupt.h"

#include "domain/algorithm/types/AlgorithmState.h"
#include "domain/interface/BesqContext.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <conio.h>
#include <windows.h>
#else
#include <csignal>
#include <poll.h>
#include <termios.h>
#endif

namespace cli_ctrl {

bool solve_interrupt_gate(BesqContext* ctx) noexcept {
    // 门控：仅求解中消费 ^C。T1 review N1（binding）：旧实现读
    // solve_progress()（BesqContext 的原子 shared_ptr handle load——内部有
    // 锁，非严格信号安全），已改为纯原子 bool 活跃标志 g_solve_active（tty
    // 控制循环启动 solver 前置位、join 后清位）——handler 只做原子 load，
    // **严格 async-signal-safe**（spec §3.2 注记的旧"pragma 级接受"不再需要）。
    // \p ctx 参数保留（历史调用/测试兼容），门控判定不依赖它。
    (void)ctx;
    return g_solve_active.load();
}

#ifdef _WIN32

/// Windows 控制台控制事件 handler（系统在专用线程调用）。
/// ^C 且求解中：置 Abort 请求 + 中断标志，返回 TRUE（已处理——进程不默认
/// 终止，控制循环随后执行 abort_solve）。
/// 其余情况（非 ^C 事件 / 非求解）：返回 FALSE → 默认处理（终止进程，与注册
/// handler 之前的 Ctrl+C 行为一致）。
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type != CTRL_C_EVENT)
        return FALSE;
    if (!solve_interrupt_gate(nullptr))
        return FALSE; // 非求解中：不拦截，走默认终止
    g_solve_interrupted.store(true);
    g_solve_control.store(SolveControlRequest::Abort);
    return TRUE;
}

#else

/// POSIX SIGINT handler（信号上下文）。
/// async-signal-safe：只做原子存储（std::atomic store = 无锁原子指令）+ 门控
/// 的纯原子 bool load（g_solve_active，T1 review N1 门控修正）——不调用任何
/// BesqContext 动作方法（abort_solve 由控制循环执行），**严格信号安全**。
/// 非求解中：恢复默认处置并重发 SIGINT → 进程按默认语义终止。标准模式：
/// raise() 时 SIGINT 仍被当前 handler 阻塞（sigaction 未设 SA_NODEFER），
/// 重发信号挂起，handler 返回后按已恢复的默认处置投递 → 终止。
extern "C" void sigint_handler(int) {
    if (!solve_interrupt_gate(nullptr)) {
        ::signal(SIGINT, SIG_DFL);
        ::raise(SIGINT);
        return;
    }
    g_solve_interrupted.store(true);
    g_solve_control.store(SolveControlRequest::Abort);
}

#endif

void register_solve_interrupt_handler() noexcept {
#ifdef _WIN32
    // 进程级注册一次（同 routine 重复注册为 no-op），不注销（进程生命周期）。
    ::SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    struct sigaction sa {};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    // 不设 SA_RESTART：被中断的阻塞系统调用返回 EINTR（求解线程无阻塞系统
    // 调用，实际无影响）；不设 SA_NODEFER：handler 执行期间 SIGINT 被阻塞——
    // 非求解的恢复默认 + 重发模式依赖该行为（重发信号挂起，handler 返回后
    // 按默认处置投递）。
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
#endif
}

bool try_read_stdin_char(char& out) noexcept {
#ifdef _WIN32
    // _kbhit/_getch 为控制台输入原语（stdin 重定向时 _kbhit 恒 0 → false）。
    // 控制循环仅在 tty 下运行，此路径即控制台读键。
    if (::_kbhit()) {
        const int c = ::_getch();
        if (c == EOF)
            return false;
        out = static_cast<char>(c);
        return true;
    }
    return false;
#else
    // poll 0 超时：无事件 → false（不阻塞）；EOF（重定向）/EINTR → false。
    struct pollfd pfd { STDIN_FILENO, POLLIN, 0 };
    const int r = ::poll(&pfd, 1, 0);
    if (r <= 0 || !(pfd.revents & POLLIN))
        return false;
    char b = 0;
    if (::read(STDIN_FILENO, &b, 1) != 1)
        return false;
    out = b;
    return true;
#endif
}

// ====================================================================
// StdinCtrlGuard — RAII 终端原始化（控制循环 Task 2 包住求解期间）
// ====================================================================
struct StdinCtrlGuard::Impl {
#ifdef _WIN32
    HANDLE console = INVALID_HANDLE_VALUE;
    DWORD saved_mode = 0;
    bool active = false;
#else
    struct termios saved;
    bool saved_ok = false;
#endif
};

StdinCtrlGuard::StdinCtrlGuard() noexcept : _impl(std::make_unique<Impl>()) {
#ifdef _WIN32
    // 仅当 stdin 是控制台时生效（管道/文件 → GetConsoleMode 失败 → no-op）。
    _impl->console = ::GetStdHandle(STD_INPUT_HANDLE);
    if (_impl->console != INVALID_HANDLE_VALUE && ::GetConsoleMode(_impl->console, &_impl->saved_mode)) {
        // 清行缓冲 + 回显：控制字符逐键到达（_getch 路径）；保留
        // ENABLE_PROCESSED_INPUT：^C 仍生成 CTRL_C_EVENT（handler 消费）。
        const DWORD mode = _impl->saved_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        if (::SetConsoleMode(_impl->console, mode))
            _impl->active = true;
    }
#else
    // 仅当 stdin 是 tty 时生效（tcgetattr 失败 → no-op）。
    _impl->saved_ok = ::tcgetattr(STDIN_FILENO, &_impl->saved) == 0;
    if (_impl->saved_ok) {
        struct termios raw = _impl->saved;
        raw.c_lflag &= ~(ICANON | ECHO); // 非规范：逐键到达（^P/^R/^S 无需 Enter）
        raw.c_iflag &= ~IXON;            // 禁流控：^S(0x13) 不被吞（用户裁决）
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
            _impl->saved_ok = false; // 设置失败 → 无状态可恢复
    }
#endif
}

StdinCtrlGuard::~StdinCtrlGuard() {
#ifdef _WIN32
    if (_impl->active)
        ::SetConsoleMode(_impl->console, _impl->saved_mode);
#else
    if (_impl->saved_ok)
        ::tcsetattr(STDIN_FILENO, TCSANOW, &_impl->saved);
#endif
}

} // namespace cli_ctrl
