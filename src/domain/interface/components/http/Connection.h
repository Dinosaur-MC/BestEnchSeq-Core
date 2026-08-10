#pragma once
#include "HttpCommon.h"
#include "HttpParser.h"
#include "SseStream.h"
#include "StreamChannel.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace web {

/// 慢客户端/空闲连接超时（spec §4.2 资源上限）：
///   kStalledReadTimeout — 请求已部分到达（解析未完成）且 5s 无进展 → 关闭。
///   kKeepAliveTimeout   — keep-alive 空闲连接 30s 无活动 → 关闭。
/// SSE 流模式的心跳阈值复用 Connection::heartbeat_interval（默认 15s）。
inline constexpr auto kStalledReadTimeout = std::chrono::seconds(5);
inline constexpr auto kKeepAliveTimeout = std::chrono::seconds(30);

/// 输出缓冲上限：pipelining 客户端不读响应时，积压输出超过该值 → 关闭连接
/// （内存有界，防无限推高）。
inline constexpr size_t kMaxOutBytes = 8 * 1024 * 1024;

/// 一条连接的运行状态机（归属某个 home loop 线程，单线程访问，无需锁）。
/// process() 每次最多推进：解析 →（缺数据时）读一块 →（完整请求则）分发 → 写一块。
/// 返回 true 表示本调用读了数据或分发了请求，false 表示 would-block/无进展。
/// 连接默认为 keep-alive：一个请求处理完后清空缓冲，等待下一条请求。
/// 收到 `is_stream` 响应后连接转为 SSE 流模式：不再读请求，只经
/// `push_sse_frame()` 把 SseStream 缓冲中的帧写出（写失败 → 关闭检测对端断开）。
///
/// 连接同时实现 StreamChannel：SSE events handler 通过 `req.stream`（= 本连接）
/// 把 SseHub 帧投递进来。`post_frame` 是跨线程入口（solve worker 调用），它只把帧
/// 交给 `_frame_sink`（Reactor 注入：post 到 home loop）；真正的帧入缓冲 + 写出在
/// `push_sse_frame(std::string)` 里，运行于 loop 线程，符合连接零锁设计。
///
/// 超时（I-3）：每次 recv/send 进展更新 `_last_activity`；poller 每 ~1s 清扫时经
/// Reactor 投递 `sweep_check(now)`（home loop 线程执行，零锁）决定心跳/关闭。
class Connection : public StreamChannel, public std::enable_shared_from_this<Connection> {
public:
    using Router = std::function<HttpResponse(const HttpRequest&)>;

    /// 超时清扫结果（Reactor::check_timeout 执行）：
    ///   None      — 未到期，保持现状；
    ///   Heartbeat — SSE 流空闲超 heartbeat_interval → flush 心跳 `: ping`
    ///                （写失败 → 对端断开检测关闭）；
    ///   Close     — 慢读/空闲 keep-alive 超时 → 关闭连接。
    enum class SweepAction { None, Heartbeat, Close };

    Connection(int fd, std::string id);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return _fd; }
    const std::string& id() const { return _id; }
    bool alive() const { return _alive; }
    /// 卸下 socket 所有权（服务器注销路径专用）：置 _fd = -1，使 ~Connection
    /// 不再关闭 socket——关闭权移交 HttpServer 的延迟关闭队列（poller 线程
    /// drain，保证 select 快照中的 fd 永不提前关闭）。仅经 Reactor::remove_
    /// connection / close_all（服务器路径）调用；单元测试直调/从未注册的连接
    /// 不卸下，析构仍自行关闭。
    void detach_fd() { _fd = -1; }
    /// 是否处于 SSE 流模式。
    bool streaming() const { return _stream != nullptr; }
    /// keep-alive 连接可接受输入时即应读（对端 FIN 后不再读）；流模式不再读请求。
    bool wants_read() const { return _alive && !_stream && !_pending_eof; }
    /// 有积压输出待写（流模式下空 → 暂停 WRITABLE 兴趣）。
    bool wants_write() const { return !_out.empty(); }

    /// 一次非阻塞推进。router 提供分发。超时由调用方（Reactor）管。
    bool process(const Router& router);
    /// 逻辑关闭：翻 _alive + 触发 on_close 回调，不关 socket。服务器路径的
    /// sock_close 由 HttpServer 延迟关闭队列（poller 线程 drain）执行——严格
    /// 晚于 Reactor::remove_connection 的注销（on_closed → unregister_fd，
    /// pmutex 内擦 home_of/want_write + 关闭入队）与 detach_fd()，保证 select
    /// 快照中的 fd 永不提前关闭、已关闭句柄不残留注册表。仅单元测试直调/从未
    /// 注册的连接由 ~Connection 自行关闭（未 detach）。
    void close();

    /// 升级为 SSE 流模式。返回 true 表示已挂载（连接存活且尚未流模式），否则 false。
    bool set_stream(std::shared_ptr<SseStream> sse);
    /// 把 SseStream 缓冲中的帧写出（Reactor 帧事件回调调用）。空缓冲时按
    /// `heartbeat_interval` 节流注入心跳 `: ping`。写失败 → 关闭（对端断开检测）。
    void push_sse_frame();
    /// 追加一条完整 SSE 帧到流缓冲并立即 flush 到线（loop 线程调用；Reactor 帧事件）。
    void push_sse_frame(std::string frame);

    /// StreamChannel：把一帧交给 home-loop 帧汇（Reactor 注入）；未设置 sink 时静默丢弃。
    void post_frame(std::string frame) override;
    /// Reactor 注入帧汇（捕获 shared_ptr/weak_ptr 而非 fd，避免 fd 复用串扰）。
    void set_frame_sink(std::function<void(std::string)> sink);
    /// StreamChannel：连接关闭时触发一次（客户端断开/服务端关闭）。控制器用它退订 SseHub。
    void on_close(std::function<void()> cb) override;

    /// SSE 心跳间隔：流缓冲空闲超过该时长且无新帧 → 注入 `: ping` 注释帧。
    /// 也是 poller 清扫判 SSE 空闲的阈值（见 sweep_check）。
    std::chrono::milliseconds heartbeat_interval = std::chrono::milliseconds(15000);

    /// 超时清扫（I-3）：在 `now` 时刻评估本连接是否到期。只在归属 loop 线程
    /// 调用（Reactor::check_timeout）。见 SweepAction 说明。
    SweepAction sweep_check(std::chrono::steady_clock::time_point now) const;

private:
    void drain_out();    // 尽力写 _out；n==-1（硬错误/对端断开）→ 关闭
    void flush_stream(); // 心跳 + 取走流缓冲 → drain_out()
    void touch();        // 任何 recv/send 进展 → 刷新 _last_activity

    std::string _remote; // 对端 IP（getpeername，构造时捕获一次）
    int _fd;
    std::string _id;
    bool _alive = true;
    bool _pending_eof = false;                            // 输出清空后关闭（对端 FIN / Connection: close / 400/413）
    bool _partial = false;                                // 请求已部分到达（半请求/半 body/管道残留）→ 慢读计时
    bool _sent_continue = false;                          // 本请求已发过 `100 Continue`（防重复；Complete 时复位）
    std::string _in;                                      // 未消费输入缓冲
    std::string _out;                                     // 待写输出缓冲
    HttpParser _parser;                                   // 增量解析器（内部保留半请求状态）
    std::shared_ptr<SseStream> _stream;                   // 非空 = SSE 流模式
    std::chrono::steady_clock::time_point _last_write;    // 最近一次写出帧的时间（心跳节流）
    std::chrono::steady_clock::time_point _last_activity; // 最近一次 recv/send 进展
    std::function<void(std::string)> _frame_sink;         // Reactor 注入的 home-loop 帧汇
    std::function<void()> _on_close;                      // 连接关闭回调（触发一次后清空）
};

} // namespace web
