#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace web {

/// task 级 SSE 发布/订阅：solve worker 发布帧，订阅方（Reactor 的 SSE 连接）接收。
/// 订阅回调在线程安全下调用；回调应把帧投递到目标连接的 home loop。
class SseHub {
public:
    using SubId = uint64_t;
    using FrameFn = std::function<void(const std::string& task_id, std::string frame)>;

    /// 订阅 task 帧；返回订阅 id（用于取消）。task 完成/失败后由订阅方取消。
    /// replay_last=true：订阅时立即重放该任务最近一帧——根治"订阅晚于发布 →
    /// 帧丢失"竞态（终态帧对迟到订阅者可见）。仅任务键语义使用（终态帧
    /// 重放有意义）；实时流键不得开启——旧帧跨订阅者泄漏。
    /// （B3 起 hub 纯任务键：/api/logs* 与 logs 键已随 LogsController 删除。）
    SubId subscribe(const std::string& task_id, FrameFn fn, bool replay_last = false);

    /// 取消单个订阅；无副作用当 task/订阅不存在。
    void unsubscribe(const std::string& task_id, SubId id);

    /// 清空某 task 的全部订阅（task 收尾调用）。
    void unsubscribe_all(const std::string& task_id);

    /// 清空全部订阅（WebModule 关机调用）。逐片 swap 到局部变量后锁外析构，
    /// 不打断在途 publish（publish 持锁拷贝列表后锁外调用）。析构订阅回调会释放其
    /// 捕获的 shared_ptr<Connection>，触发连接 close() → on_close（控制器退订）。
    /// 必须在 Reactor/控制器销毁与 solve worker join 之前调用（见 WebModule.h 注释）。
    void clear();

    /// 发布一帧（solve worker 调用，线程安全）。每任务保留最后一帧：
    /// 迟到订阅者（订阅晚于发布）在 subscribe 时立即收到重放——根治
    /// "completed 帧先于订阅发布即丢失" 的竞态（无订阅者发布不再丢弃）。
    void publish(const std::string& task_id, std::string frame);

    /// 订阅数（测试/状态用）。
    size_t subscriber_count(const std::string& task_id) const;

private:
    struct Sub {
        SubId id;
        FrameFn fn;
    };
    /// 按键分片：每片独立 mutex + 子表 + 末帧缓存（锁竞争 ÷64；clear 逐片
    /// swap 后锁外析构）。
    struct Shard {
        /// mutable：subscriber_count 是 const 方法，仍需上片锁。
        mutable std::mutex mutex;
        std::unordered_map<std::string, std::vector<Sub>> subs;
        /// 每任务最后一帧（重放用；unsubscribe_all 不清除——任务收尾后迟到
        /// 订阅者仍应拿到终态帧；随 clear() 一起释放）。
        std::unordered_map<std::string, std::string> last_frame;
    };
    static constexpr size_t kShards = 64;
    static size_t shard_of(const std::string& key); // FNV-1a 取模 kShards

    std::array<Shard, kShards> _shards;
    std::atomic<SubId> _next{0};
};

} // namespace web
