#pragma once
#include <string>

namespace web {

/// 流式响应帧投递通道：控制器 SSE events handler 用它把 SseHub 帧送到客户端连接。
/// 线程安全；连接可能已关闭则静默丢弃。
class StreamChannel {
public:
    virtual ~StreamChannel() = default;
    /// 投递一帧（线程安全；连接可能已关闭则静默丢弃）。
    virtual void post_frame(std::string frame) = 0;
};

} // namespace web
