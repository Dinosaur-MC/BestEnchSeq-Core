# 工具层（`utils/`）

## 设计原则

通用工具集，**不依赖任何 domain / algorithm / registry 层**。所有组件都是纯模板头文件或自包含的.hpp/.cpp。可以被项目任何层使用。

---

## 队列家族（`queue/`）

无锁并发队列，从最快到最通用排序：

| 队列 | 生产者 | 消费者 | 内存 | 特点 |
|---|---|---|---|---|
| `SPSCQueue` | 1 | 1 | 有界环形缓冲 | 最快，零 CAS |
| `SPMCQueue` | 1 | N | 有界环形缓冲 | 单生产者，多消费者 |
| `BoundedMPMCQueue` | N | N | 有界环形缓冲 | 固定容量，无分配 |
| `SegmentedMPMCQueue` | N | N | 分段链表（无界） | 优雅降级，O(1) amortized push |

所有队列的 `try_push` / `try_pop` 操作是**等待无关**的（wait-free），在热路径上不会阻塞。阻塞变体（`push` / `pop`）内部使用 `atomic::wait` 实现零 CPU 等待。

### `IQueue.h`

```cpp
template <typename T>
struct IQueue {
    virtual bool try_push(const T&) = 0;
    virtual bool try_push(T&&) = 0;
    virtual bool try_pop(T&) = 0;
    virtual size_t size_approx() const = 0;
};
```

### `BoundedMPMCQueue.hpp`

容量模板参数在编译期固定，内部使用 DWCAS 风格的 slot 序列。适用于诊断事件等固定速率的生产者。

```cpp
template <typename T, size_t Capacity, typename Padding = CachePadding<>>
class BoundedMPMCQueue : public IQueue<T> {
    bool try_push(const T& item);       // false if full
    bool try_push(T&& item);
    bool try_pop(T& item);              // false if empty
    // emplace 系列: 在 slot 上 placement new
    template <typename... Args>
    bool try_emplace(Args&&... args);
    template <typename... Args>
    void emplace(Args&&... args);       // 阻塞
    // push/pop 阻塞变体
    void push(const T&);
    void push(T&&);
    void pop(T&);
};
```

### `SPSCQueue.hpp`

单生产者单消费者，所有操作 memory_order 最优。用于诊断写入器等单线程路径。

### `SPMCQueue.hpp`

单生产者多消费者。

### `SegmentedMPMCQueue.hpp`

无界变体，优雅处理负载尖峰。`DiagnosticsService` 使用此类型。

---

## EventLoop（`EventLoop.hpp`）

基于 `std::atomic::wait` 的事件循环，队列空时零 CPU 消耗。

```cpp
template <typename T, typename Queue = SegmentedMPMCQueue<T>,
          typename Handler = void>
class EventLoop {
    // 生命周期
    void start();                        // 启动消费线程（幂等）
    void stop(bool force = false);       // true = 丢弃剩余项
    bool is_running() const;

    // 提交
    bool try_post(T item);
    void post(T item);                   // 阻塞
    template <typename... Args>
    bool try_post_emplace(Args&&...);
    template <typename... Args>
    void post_emplace(Args&&...);        // 阻塞 placement new

    // 批量
    size_t try_post_batch(InputIt begin, InputIt end);
    void post_batch(InputIt begin, InputIt end);
};
```

便利别名：

```cpp
using MPMCEventLoop<Task>      = EventLoop<Task, SegmentedMPMCQueue<Task>>;
using BoundedEventLoop<Task, N> = EventLoop<Task, BoundedMPMCQueue<Task, N>>;
using SPSCEventLoop<Task, N>   = EventLoop<Task, SPSCQueue<Task, N>>;
```

消费线程使用 `std::jthread` + `std::stop_token` 实现协作式取消。

---

## 内存池

### `MemoryPool.hpp`

固定大小对象的单调递增分配器。

```cpp
class MemoryPool {
    MemoryPool(size_t block_size, size_t blocks_per_chunk);
    ~MemoryPool();

    void* allocate();                // O(1), 无初始化
    void deallocate(void* p);        // O(1), 归还到空闲链表
    void reset();                    // 重置整个池

    // 统计
    size_t block_size() const;
    size_t allocated() const;
    size_t capacity() const;
};
```

- 内部维护空闲链表，分配/释放 O(1)
- 大块申请（chunk）减少系统调用
- `reset()` 一次性归还所有内存，不逐个释放

### `ObjectPool.hpp`

类型安全的 MemoryPool 包装。

```cpp
template <typename T>
class ObjectPool {
    template <typename... Args>
    T* acquire(Args&&... args);       // 分配 + 构造
    void release(T* obj);             // 析构 + 归还
    void reset();
};
```

- `acquire()` 调用 placement new
- `release()` 调用析构函数
- 用于 `TTTable` 节点等大量短生命周期对象的场景

---

## 哈希工具（`HashUtils.hpp`）

```cpp
// FNV-1a 哈希（64-bit）
constexpr uint64_t fnv1a(std::string_view s) noexcept;

// 通用 hash_combine（boost 风格），用于自定义类型组合
template <typename T>
void hash_combine(size_t& seed, const T& v) noexcept;
```

`compact::EnchSet` 和 `compact::Item` 的 `std::hash` 特化使用 `hash_combine` 实现。

---

## FlatHashMap（`FlatHashMap.hpp`）

开放寻址扁平哈希表，用于算法热路径中的轻量查表。

```cpp
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class FlatHashMap {
    Value* find(const Key& k);       // 返回 nullptr 表示未找到
    bool insert(const Key& k, const Value& v);
    void clear();
    size_t size() const;
};
```

不使用 `std::unordered_map` 以避免每次插入的堆分配。

---

## 其他工具

### `EnvUtil.hpp`

```cpp
// 类型安全的环境变量读取
template <typename T>
T get_env(const char* name, T default_value = T{});
// 支持 int, float, bool, std::string_view 等类型
```

### `ExpCalculator.hpp`

```cpp
// 经验等级 ↔ 总经验点数 转换
int32_t get_exp_to_level(int32_t level);       // 升到 level 所需的总经验
int32_t get_level_from_exp(int32_t total_exp); // 从总经验反算等级
```

### `ParserUtils.hpp`

```cpp
// 文件读取
std::string read_file(const std::string& path);

// JSON 工具
json::Json parse_json_file(const std::string& path);
json::Json parse_json_string(const std::string& content);

// 字符串工具
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
bool starts_with(std::string_view s, std::string_view prefix);

// Tag 引用解析
std::string resolve_tag(const std::string& tag, const json::Json& data);
```

---

## 性能指南

| 组件 | 路径 | 特点 |
|---|---|---|
| SPSCQueue | 热路径（诊断写入） | 无 CAS，~50ns push+pop |
| BoundedMPMCQueue | 诊断事件（64 slot） | 固定容量，try 变体 wait-free |
| SegmentedMPMCQueue | 通用高吞吐 | 无界，优雅处理尖峰 |
| MemoryPool | TTTable 节点 | O(1) 分配，chunk 预申请 |
| FlatHashMap | 算法查表 | 开放寻址，零堆分配 per insert |

不保证线程安全的组件：`MemoryPool` / `ObjectPool` 不内置锁，需调用方保证外部同步。
