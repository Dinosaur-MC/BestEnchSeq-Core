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

BoundedMPMCQueue: 容量模板参数在编译期固定，适用于诊断事件等固定速率的生产者。
SPSCQueue: 单生产者单消费者，用于诊断写入器等单线程路径。
SPMCQueue: 单生产者多消费者。
SegmentedMPMCQueue: 无界变体，优雅处理负载尖峰（DiagnosticsService 使用此类型）。

### `SPSCQueue.hpp`

单生产者单消费者，所有操作 memory_order 最优。用于诊断写入器等单线程路径。

### `SPMCQueue.hpp`

单生产者多消费者。

### `SegmentedMPMCQueue.hpp`

无界变体，优雅处理负载尖峰。`DiagnosticsService` 使用此类型。

---

## EventLoop（`EventLoop.hpp`）

EventLoop 基于 std::atomic::wait，队列空时零 CPU 消耗。

便利类型别名：MPMCEventLoop / BoundedEventLoop / SPSCEventLoop

消费线程使用 std::jthread + std::stop_token 实现协作式取消。

---

## 内存池

### `MemoryPool.hpp`

MemoryPool: 固定大小对象的单调递增分配器，O(1) 分配/释放。内部维护空闲链表，reset() 一次性归还所有内存。

ObjectPool: 类型安全的 MemoryPool 包装。acquire() placement new，release() 调用析构函数。

用于 TTTable 节点等大量短生命周期对象的场景。

---

## 哈希工具（`HashUtils.hpp`）

FNV-1a 64-bit 哈希函数 + 通用 hash_combine（boost 风格）。用于 compact::EnchSet 和 compact::Item 的 std::hash 特化。

---

## FlatHashMap（`FlatHashMap.hpp`）

开放寻址扁平哈希表，用于算法热路径中的轻量查表。不使用 std::unordered_map 以避免堆分配。

---

## 其他工具

### `EnvUtil.hpp`

EnvUtil: 类型安全的环境变量读取，支持 int/float/bool/string 等类型。

### `ExpCalculator.hpp`

ExpCalculator: 经验等级 ↔ 总经验点数 转换。

### `ParserUtils.hpp`

ParserUtils: 文件读取、JSON 工具、字符串分割、Tag 引用解析。

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
