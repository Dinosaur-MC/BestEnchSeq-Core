// src/domain/interface/components/http/Connection.h
#pragma once
#include "HttpCommon.h"
#include "HttpParser.h"
#include "SseStream.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace web {

/// 一条连接的运行状态机（归属某个 home loop 线程，单线程访问，无需锁）。
/// process() 每次最多推进：解析 →（缺数据时）读一块 →（完整请求则）分发 → 写一块。
/// 返回 true 表示本调用读了数据或分发了请求，false 表示 would-block/无进展。
/// 连接默认为 keep-alive：一个请求处理完后清空缓冲，等待下一条请求。
/// 收到 `is_stream` 响应后连接转为 SSE 流模式：不再读请求，只经
/// `push_sse_frame()` 把 SseStream 缓冲中的帧写出（写失败 → 关闭检测对端断开）。
class Connection {
public:
    using Router = std::function<HttpResponse(const HttpRequest&)>;

    Connection(int fd, std::string id);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return _fd; }
    const std::string& id() const { return _id; }
    bool alive() const { return _alive; }
    /// 是否处于 SSE 流模式。
    bool streaming() const { return _stream != nullptr; }
    /// keep-alive 连接可接受输入时即应读（对端 FIN 后不再读）；流模式不再读请求。
    bool wants_read() const { return _alive && !_stream && !_pending_eof; }
    /// 有积压输出待写（流模式下空 → 暂停 WRITABLE 兴趣）。
    bool wants_write() const { return !_out.empty(); }

    /// 一次非阻塞推进。router 提供分发。超时由调用方（Reactor）管。
    bool process(const Router& router);
    void close();

    /// 升级为 SSE 流模式。返回 true 表示已挂载（连接存活且尚未流模式），否则 false。
    bool set_stream(std::shared_ptr<SseStream> sse);
    /// 把 SseStream 缓冲中的帧写出（Reactor 帧事件回调调用）。空缓冲时按
    /// `heartbeat_interval` 节流注入心跳 `: ping`。写失败 → 关闭（对端断开检测）。
    void push_sse_frame();

    /// SSE 心跳间隔：流缓冲空闲超过该时长且无新帧 → 注入 `: ping` 注释帧。
    std::chrono::milliseconds heartbeat_interval = std::chrono::milliseconds(15000);

private:
    void drain_out();            // 尽力写 _out；n==-1（硬错误/对端断开）→ 关闭
    void flush_stream();         // 心跳 + 取走流缓冲 → drain_out()

    int _fd;
    std::string _id;
    bool _alive = true;
    bool _pending_eof = false;   // 已读到 EOF（对端不再发数据；输出清空后关闭）
    std::string _in;             // 未消费输入缓冲
    std::string _out;            // 待写输出缓冲
    HttpParser _parser;          // 增量解析器（内部保留半请求状态）
    std::shared_ptr<SseStream> _stream;                 // 非空 = SSE 流模式
    std::chrono::steady_clock::time_point _last_write;  // 最近一次写出帧的时间（心跳节流）
};

} // namespace web
