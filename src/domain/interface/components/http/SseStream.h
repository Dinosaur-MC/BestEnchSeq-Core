#pragma once
#include <string>
#include <string_view>
#include <utility>

namespace web {

/// 单条连接的 SSE 帧缓冲。帧格式（SSE over HTTP）：
///   event: <type>\n
///   data: <json>\n
///   \n
/// 心跳为注释帧 `: ping`（无 event/data，客户端仅用于保活与断开检测）。
/// 缓冲由 Connection 持有；写入方（WebSolveService 帧出口 / 心跳定时器）累加帧，
/// 再经 `Connection::push_sse_frame()` 一次性写出（`drain()` 取走并清空）。
class SseStream {
public:
    explicit SseStream(std::string id) : _id(std::move(id)) {}

    /// 追加一帧：`event: <type>\ndata: <json>\n\n`。
    void frame(std::string_view type, const std::string& json_data) {
        _buf += "event: ";
        _buf += type;
        _buf += "\ndata: ";
        _buf += json_data;
        _buf += "\n\n";
    }

    /// 追加一条已格式化好的完整 SSE 帧（SseHub 发布端直接送来的帧串）。
    void raw(std::string frame) { _buf += std::move(frame); }

    /// 追加心跳注释帧：`: ping\n\n`。
    void ping() { _buf += ": ping\n\n"; }

    bool empty() const { return _buf.empty(); }
    size_t size() const { return _buf.size(); }

    /// 取走全部缓冲并清空。
    std::string drain() {
        std::string out = std::move(_buf);
        _buf.clear();
        return out;
    }

    const std::string& id() const { return _id; }

private:
    std::string _id;
    std::string _buf;
};

} // namespace web
