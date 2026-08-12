#include "SseHub.h"

namespace web {

size_t SseHub::shard_of(const std::string& key) {
    // FNV-1a 64 位（与 RateLimiter 同款算法）；取模 kShards 定位分片，
    // 同任务键恒落同片，跨片无共享状态。
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return static_cast<size_t>(h % kShards);
}

SseHub::SubId SseHub::subscribe(const std::string& task_id, FrameFn fn, bool replay_last) {
    auto& shard = _shards[shard_of(task_id)];
    std::string replay;
    SubId id;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        id = ++_next; // 原子自增：分片后无全局锁，id 跨片唯一
        shard.subs[task_id].push_back(Sub{id, fn});
        if (replay_last) {
            auto it = shard.last_frame.find(task_id);
            if (it != shard.last_frame.end())
                replay = it->second;
        }
    }
    // 锁外重放（回调可能 re-enter hub；与 publish 的锁外调用一致）。
    // 根治"订阅晚于发布 → 帧丢失"竞态（SSE completed 帧等终态帧对
    // 迟到订阅者可见，见 test_web_integration P2 记录）。仅任务键开启；
    // 实时流键（logs）默认关闭——旧帧不得跨订阅者泄漏。
    if (!replay.empty())
        fn(task_id, std::move(replay));
    return id;
}

void SseHub::unsubscribe(const std::string& task_id, SubId id) {
    auto& shard = _shards[shard_of(task_id)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.subs.find(task_id);
    if (it == shard.subs.end())
        return;
    auto& vec = it->second;
    for (auto sit = vec.begin(); sit != vec.end(); ++sit) {
        if (sit->id == id) {
            vec.erase(sit);
            break;
        }
    }
    if (vec.empty())
        shard.subs.erase(it);
}

void SseHub::unsubscribe_all(const std::string& task_id) {
    auto& shard = _shards[shard_of(task_id)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    shard.subs.erase(task_id); // 末帧保留：任务收尾后迟到订阅者仍可拿到终态帧
}

void SseHub::clear() {
    // 逐片 swap 后锁外析构：清空后 publish/unsubscribe 均为无操作，不打断在途
    // publish（publish 持锁拷贝列表后锁外调用）。析构 Sub 释放其 FrameFn 捕获的
    // shared_ptr<Connection> → 触发连接 close() → on_close 回调（控制器退订）。
    // 连接与控制器在本方法调用点均存活（WebModule 析构体在 Impl 成员析构前
    // 调用），故安全。
    for (auto& shard : _shards) {
        decltype(shard.subs) subs;
        decltype(shard.last_frame) frames;
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            subs.swap(shard.subs);
            frames.swap(shard.last_frame);
        }
        subs.clear();
        frames.clear(); // 重放帧随清空释放
    }
}

void SseHub::publish(const std::string& task_id, std::string frame) {
    // Copy the subscriber list under the lock, then invoke callbacks outside it:
    // a callback may unsubscribe / re-subscribe without deadlocking the hub.
    auto& shard = _shards[shard_of(task_id)];
    std::vector<Sub> subs;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        // 每任务保留最后一帧（迟到订阅者重放）；任务收尾后不清除——
        // 订阅晚于终态发布的客户端仍能拿到 completed/failed 帧。
        shard.last_frame[task_id] = frame;
        auto it = shard.subs.find(task_id);
        if (it == shard.subs.end())
            return;
        subs = it->second;
    }
    for (const auto& sub : subs)
        sub.fn(task_id, frame);
}

size_t SseHub::subscriber_count(const std::string& task_id) const {
    const auto& shard = _shards[shard_of(task_id)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.subs.find(task_id);
    return it == shard.subs.end() ? 0 : it->second.size();
}

} // namespace web
