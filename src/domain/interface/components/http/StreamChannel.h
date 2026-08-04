#pragma once
#include <functional>
#include <string>

namespace web {

/// 流式响应帧投递通道：控制器 SSE events handler 用它把 SseHub 帧送到客户端连接。
/// 线程安全；连接可能已关闭则静默丢弃。
class StreamChannel {
public:
    virtual ~StreamChannel() = default;
    /// 投递一帧（线程安全；连接可能已关闭则静默丢弃）。
    virtual void post_frame(std::string frame) = 0;
    /// 连接关闭时回调（客户端断开/服务端关闭）。可注册以清理资源（如退订 SseHub）。
    /// 只触发一次，且仅当连接真正关闭后触发。实现方保证回调在连接拆毁时被调用。
    virtual void on_close(std::function<void()> cb) = 0;
};

} // namespace web
