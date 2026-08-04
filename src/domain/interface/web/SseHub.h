#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace web {

/// task 级 SSE 发布/订阅：solve worker 发布帧，订阅方（Reactor 的 SSE 连接）接收。
/// 订阅回调在线程安全下调用；回调应把帧投递到目标连接的 home loop。
class SseHub {
public:
    using SubId = uint64_t;
    using FrameFn = std::function<void(const std::string& task_id, std::string frame)>;

    /// 订阅 task 帧；返回订阅 id（用于取消）。task 完成/失败后由订阅方取消。
    SubId subscribe(const std::string& task_id, FrameFn fn);

    /// 取消单个订阅；无副作用当 task/订阅不存在。
    void unsubscribe(const std::string& task_id, SubId id);

    /// 清空某 task 的全部订阅（task 收尾调用）。
    void unsubscribe_all(const std::string& task_id);

    /// 发布一帧（solve worker 调用，线程安全）。
    void publish(const std::string& task_id, std::string frame);

    /// 订阅数（测试/状态用）。
    size_t subscriber_count(const std::string& task_id) const;

private:
    struct Sub {
        SubId id;
        FrameFn fn;
    };
    mutable std::mutex _mutex;
    std::unordered_map<std::string, std::vector<Sub>> _subs;
    SubId _next = 0;
};

} // namespace web
