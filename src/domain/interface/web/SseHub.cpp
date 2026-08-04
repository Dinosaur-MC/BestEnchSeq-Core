#include "SseHub.h"

namespace web {

SseHub::SubId SseHub::subscribe(const std::string& task_id, FrameFn fn) {
    std::lock_guard<std::mutex> lock(_mutex);
    SubId id = ++_next;
    _subs[task_id].push_back(Sub{id, std::move(fn)});
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
    {
        std::lock_guard<std::mutex> lock(_mutex);
        subs.swap(_subs);
    }
    // 锁外析构：清空后 publish/unsubscribe 均为无操作。析构 Sub 释放其 FrameFn 捕获的
    // shared_ptr<Connection> → 触发连接 close() → on_close 回调（控制器退订）。连接与
    // 控制器在本方法调用点均存活（WebModule 析构体在 Impl 成员析构前调用），故安全。
    subs.clear();
}

void SseHub::publish(const std::string& task_id, std::string frame) {
    // Copy the subscriber list under the lock, then invoke callbacks outside it:
    // a callback may unsubscribe / re-subscribe without deadlocking the hub.
    std::vector<Sub> subs;
    {
        std::lock_guard<std::mutex> lock(_mutex);
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
