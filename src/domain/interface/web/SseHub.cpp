#include "SseHub.h"

namespace web {

SseHub::SubId SseHub::subscribe(const std::string& task_id, FrameFn fn,
                                bool replay_last) {
    std::string replay;
    SubId id;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        id = ++_next;
        _subs[task_id].push_back(Sub{id, fn});
        if (replay_last) {
            auto it = _last_frame.find(task_id);
            if (it != _last_frame.end())
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
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _subs.find(task_id);
    if (it == _subs.end()) return;
    auto& vec = it->second;
    for (auto sit = vec.begin(); sit != vec.end(); ++sit) {
        if (sit->id == id) {
            vec.erase(sit);
            break;
        }
    }
    if (vec.empty()) _subs.erase(it);
}

void SseHub::unsubscribe_all(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(_mutex);
    _subs.erase(task_id);
}

void SseHub::clear() {
    decltype(_subs) subs;
    decltype(_last_frame) frames;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        subs.swap(_subs);
        frames.swap(_last_frame);
    }
    // 锁外析构：清空后 publish/unsubscribe 均为无操作。析构 Sub 释放其 FrameFn 捕获的
    // shared_ptr<Connection> → 触发连接 close() → on_close 回调（控制器退订）。连接与
    // 控制器在本方法调用点均存活（WebModule 析构体在 Impl 成员析构前调用），故安全。
    subs.clear();
    frames.clear();  // 重放帧随清空释放
}

void SseHub::publish(const std::string& task_id, std::string frame) {
    // Copy the subscriber list under the lock, then invoke callbacks outside it:
    // a callback may unsubscribe / re-subscribe without deadlocking the hub.
    std::vector<Sub> subs;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        // 每任务保留最后一帧（迟到订阅者重放）；任务收尾后不清除——
        // 订阅晚于终态发布的客户端仍能拿到 completed/failed 帧。
        _last_frame[task_id] = frame;
        auto it = _subs.find(task_id);
        if (it == _subs.end()) return;
        subs = it->second;
    }
    for (const auto& sub : subs) sub.fn(task_id, frame);
}

size_t SseHub::subscriber_count(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _subs.find(task_id);
    return it == _subs.end() ? 0 : it->second.size();
}

} // namespace web
