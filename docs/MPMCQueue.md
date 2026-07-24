# 无锁 MPMC 队列：设计、实现与使用指南

> 目标平台：LLVM Clang++ 22 / C++20  
> 设计约束：完全无锁（zero mutex）、零阻塞、百万并发友好  
> 仓库位置：`src/common/utils/queue/BoundedMPMCQueue.hpp` · `src/common/utils/queue/SegmentedMPMCQueue.hpp`

---

## 目录

1. [架构概述](#1-架构概述)
2. [Version A：BoundedMPMCQueue](#2-version-aboundedmpmcqueue)
3. [Version B：SegmentedMPMCQueue](#3-version-bsegmentedmpmcqueue)
4. [统一接口层：IQueue 与 QueueType](#4-统一接口层iqueue-与-queuetype)
5. [算法正确性证明](#5-算法正确性证明)
6. [内存序精解](#6-内存序精解)
7. [性能模型](#7-性能模型)
8. [使用指南](#8-使用指南)
9. [百万并发注意事项](#9-百万并发注意事项)
10. [与现有队列对比](#10-与现有队列对比)
11. [FAQ / 常见陷阱](#11-faq--常见陷阱)

---

## 1. 架构概述

本仓库提供两套 MPMC（多生产者多消费者）无锁队列，覆盖从密集探针到可靠传输的全场景：

```
┌────────────────────────────────────────────────────────┐
│                    MPMC Queue Family                   │
├─────────────────────┬──────────────────────────────────┤
│  BoundedMPMCQueue   │   SegmentedMPMCQueue             │
│  定长 · 零阻塞      │   动态扩容 · 永不丢数据          │
│  溢出丢弃           │   OOM 抛 bad_alloc               │
│  无 CAS 竞争路径    │   纯 CAS 块链接                  │
│  探针/监控/指标     │   日志/IO/事件流                 │
└─────────────────────┴──────────────────────────────────┘
```

### 核心设计原则

1. **无锁路径全覆盖**：每个 `try_push`/`try_pop` 路径上零 mutex、零 spinlock、零阻塞系统调用
2. **消除伪共享**：每个可能被不同核心同时访问的原子变量独占 64 字节 cache line
3. **严格的内存序裁剪**：只在必要的位置使用 acquire/release，绝不滥用 seq_cst
4. **序列号协议**：每个槽位的单一 `uint64_t` 编码"空闲/就绪/已消费"三重状态，天然防 ABA

### 共同的线程安全契约

| 操作 | 安全并发度 |
|------|-----------|
| `try_push()` | 任意数量的并发生产者 |
| `try_pop()` | 任意数量的并发消费者 |
| 同一位置的 push + pop | 安全（序列号协议保证） |
| 析构函数 | 仅当无其他线程访问时调用 |
| `size()` / `empty()` | 返回近似值（存疑快照） |

---

## 2. Version A：BoundedMPMCQueue

### 2.1 算法：Vyukov Bounded MPMC

基于 Dmitry Vyukov 2008 年提出的有界 MPMC 队列，使用**序列号 + 环形数组** 实现。

```
         head_ (producer)             tail_ (consumer)
              │                             │
              ▼                             ▼
┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐
│ S:0  │ S:1  │ S:2  │ S:3  │ S:4  │ S:5  │ S:6  │ S:7  │  ← sequence
│      │      │      │ D:3  │ D:4  │ D:5  │      │      │  ← data
└──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
       pos=3               pos=6
       (producer           (consumer
        writing)            reading)
```

### 2.2 序列号生命周期

每个槽位的 `sequence` 编码其在时间轴上的位置：

```
pos=3 的槽位生命周期：

  seq = 3          seq = 4          seq = 3 + Capacity(8) = 11
  ───────●────────────●────────────────────●─────────►
     空闲          已写入            已消费（下一轮空闲）
     ↑              ↑                   ↑
  生产者抢占     生产者发布           消费者释放
  (CAS head)    (release store)      (release store)
```

生产者判断（diff = seq - pos）：
- **diff == 0**：槽位空闲 → 抢 CAS，写入，发布 seq = pos + 1
- **diff < 0**：队列满 → 丢数据
- **diff > 0**：其他生产者抢先了 → 重读 head

消费者判断（diff = seq - (pos + 1)）：
- **diff == 0**：数据就绪 → 抢 CAS，读出，发布 seq = pos + Capacity
- **diff < 0**：队列空
- **diff > 0**：其他消费者抢先了 → 重读 tail

### 2.3 push 路径

```cpp
bool push(T value) {
    pos = head_.load(relaxed)          // 猜测起始位置
    loop:
      slot = slots_[pos & mask]
      seq = slot.sequence.load(acquire)
      diff = seq - pos
      if diff == 0:                    // 槽位属于我
        if head_.CAS(pos, pos+1):       // 抢到独占权
          construct(value)             // placement new
          slot.sequence.store(pos+1, release)  // 发布给消费者
          return true
      else if diff < 0:                // 满
        return false
      else:                            // 被抢
        pos = head_.load(relaxed)
```

### 2.4 pop 路径

```cpp
bool pop(T& out) {
    pos = tail_.load(relaxed)
    loop:
      slot = slots_[pos & mask]
      seq = slot.sequence.load(acquire)
      diff = seq - (pos + 1)
      if diff == 0:                    // 数据就绪
        if tail_.CAS(pos, pos+1):       // 抢到独占权
          out = move(slot.data)
          slot.data.~T()
          slot.sequence.store(pos + Capacity, release)  // 释放给生产者
          return true
      else if diff < 0:                // 空
        return false
      else:                            // 被抢
        pos = tail_.load(relaxed)
}
```

### 2.5 关键特性

- **零阻塞**：满/空时直接返回 false，绝不 yield、spin 或 block
- **单 CAS**：push/pop 各仅一次 CAS，无 fetch_add 或更重的 RMW
- **容量无关延迟**：O(1) push/pop，与容量大小无关
- **原始存储**：使用 `alignas(T) unsigned char[sizeof(T)]`，不要求 T 有默认构造函数

---

## 3. Version B：SegmentedMPMCQueue

### 3.1 算法：分块链表 + 全局 Ticket

```
Block 0 (base=0)     Block 1 (base=1024)  Block 2 (base=2048)
┌────┬────┬──┰──┐    ┌────┬────┬──┰──┐    ┌────┬────┬──┰──┐
│ S0 │ S1 │… │S3│    │ S0 │ S1 │… │S3│    │ S0 │ S1 │… │S3│
│ D0 │ D1 │… │D3│ →  │ D0 │ D1 │… │D3│ →  │ D0 │ D1 │… │D3│
└────┴────┴──┰──┘    └────┴────┴──┰──┘    └────┴────┴──┰──┘
  ↑ head_block_         ↑ tail_block_         ↑ root_block_
  (consumer)            (producer)            (immutable)
```

每个 Block 内使用与 BoundedMPMCQueue 完全相同的序列号协议。

全局 `enqueue_pos_` / `dequeue_pos_` 作为**发放 ticket**（而非位置 CAS），
保证全局 FIFO 顺序。

### 3.2 块链接：CAS 式无锁分配

```cpp
// 完全无锁的块分配，零 mutex
auto* new_block = new Block(block->base_ticket + BlockSize);
Block* expected = nullptr;

if (block->next.compare_exchange_strong(
        expected, new_block,
        memory_order_release,     // 成功：发布新块
        memory_order_relaxed)) {  // 失败：无需序化
    // 我链接成功了
    next = new_block;
} else {
    // 另一个生产者先一步链接了，丢弃我们的分配
    delete new_block;
    next = expected;  // 使用已有的块
}
```

**为什么 `delete` 是安全的**：CAS 失败的块从未被任何其他线程看到
（从未写入 `block->next`），所以它是完全私有的。

### 3.3 遍历安全网

由于 `head_block_` / `tail_block_` 是**尽力提示**（best-effort hint），
遍历后额外验证 `pos >= block->base_ticket`：

```cpp
// 遍历循环后，检查是否越过了目标块
if (pos < block->base_ticket) [[unlikely]] {
    // head/tail hint 超前于我们的位置 —— 从 root 重新扫描
    block = root_block_;          // 持久根指针，永远有效
    while (pos >= block->base_ticket + BlockSize) {
        block = block->next.load(acquire);
    }
}
```

这个安全网在以下场景中触发：
- **慢生产者**：拿了 ticket 后被长时间抢占，醒来时 tail 已远超前
- **从 tail 回退到 head**：head 也已越过所需块
- **CAS 竞争**：块遍历中 head/tail 被其他线程推进

### 3.4 内存回收策略

| 层次 | 机制 | 触发频次 |
|------|------|---------|
| 每个 slot 的数据 | placement new → 析构 | 每个元素 |
| 已消费的 slot | 由 pop 调用 `~T()` | 每个元素 |
| 未链接的块 | `delete`（CAS 失败的分配） | 罕见（块分配竞争时） |
| 链上已消费的块 | 由析构函数遍历删除 | 仅队列销毁时 |

**关键设计选择**：块在并发访问期间绝不释放。只有队列析构时，
才沿 `next` 链遍历所有块并 `delete`。这避免了复杂的无锁内存回收
（hazard pointers / EBR），同时保持了数据路径的完全无锁。

### 3.5 BlockSize 的选择

| BlockSize | 块分配频率（1M 次 push） | 浪费内存 | 遍历开销 |
|-----------|------------------------|---------|---------|
| 64 | 15625 次 | 低 | 最多 15625 步 |
| 256 | 3906 次 | 中 | 最多 3906 步 |
| 1024（默认） | 977 次 | 较高 | 最多 977 步 |
| 4096 | 244 次 | 高 | 最多 244 步 |

**建议**：
- IO/日志场景：使用默认 1024，块分配开销可忽略
- 极低延迟场景：使用 4096，减少块遍历
- 内存敏感场景：使用 256，降低浪费

---

## 4. 统一接口层：IQueue 与 QueueType

所有队列类型继承自 `IQueue<T>` 虚接口（位于 `src/common/utils/queue/IQueue.h`），并提供统一的
`try_push(const T&) -> bool`、`try_push(T&&) -> bool`、`try_pop(T&) -> bool` 签名。

### QueueType 概念

```cpp
template <typename Q, typename T>
concept QueueType = requires(Q& q, const T& cval, T& val) {
    { q.try_push(cval) }             -> std::same_as<bool>;
    { q.try_push(std::move(val)) }   -> std::same_as<bool>;
    { q.try_pop(val) }               -> std::same_as<bool>;
    { q.size() }                     -> std::convertible_to<size_t>;
    { q.empty() }                    -> std::convertible_to<bool>;
};
```

`EventLoop<T, Queue, Handler>` 使用此概念约束其队列参数。

### QueueAdaptor

`QueueAdaptor<T, ConcreteQueue>` 包装具体队列为 `IQueue<T>` 多态接口：

```cpp
QueueAdaptor<int, SPSCQueue<int, 256>> adapted;
IQueue<int>& q = adapted;   // runtime-polymorphic access
q.try_push(42);
```

---

## 5. 算法正确性证明

### 4.1 不变式

```
BoundedMPMCQueue:
  ∀ slot i: sequence[i] ∈ {i, i+1, i+Capacity}       (mod 2*Capacity)
  head_ ∈ [0, +∞)
  tail_ ∈ [0, +∞)
  tail_ ≤ head_ ≤ tail_ + Capacity

SegmentedMPMCQueue:
  enqueue_pos_ ≥ dequeue_pos_                          (不丢失)
  ∀ Block B: B.sequences[i] ∈ {B+i, B+i+BlockSize, B+i+2*BlockSize}
  root_block_ = Block₀                                 (永远有效)
  tail_block_ ≥ head_block_ ≥ root_block_              (链单调增长)
```

### 4.2 无竞争安全

**push 的排他性**：CAS on head_（Bounded）或 fetch_add（Segmented）
保证每个位置恰被一个生产者获取。序列号协议保证写入完成前消费者不可见。

**pop 的排他性**：CAS on tail_（Bounded）或 dequeue_pos_（Segmented）
保证每个元素恰被一个消费者获取。

**无 ABA**：uint64_t 序列号单调递增——即使 slot 位置被循环使用
（Bounded）或不同 Block（Segmented），序列号始终递增且唯一。

### 4.3 内存序保证

生产者 `slot.sequence.store(pos + 1, release)` 与
消费者 `slot.sequence.load(acquire)` 构成 **release-acquire pair**：

```
Producer:                          Consumer:
  data.write (plain store)           seq.load(acquire) ───┐
  seq.store(release) ──synchronizes──→                     ├── data.read (visible)
                                                         ───┘
```

因此消费者读到的 `seq` 值保证能看到生产者写入的完整数据。

---

## 6. 内存序精解

### 5.1 BoundedMPMCQueue

```cpp
// push
head_.load(relaxed)               // ① 起始猜测
slot.sequence.load(acquire)       // ② 看到消费者前次的 release
head_.CAS(relaxed)                // ③ CAS 提供原子独占
placement new                     // ④ 普通写
slot.sequence.store(release)      // ⑤ 发布④给消费者

// pop
tail_.load(relaxed)               // ① 起始猜测
slot.sequence.load(acquire)       // ② 看到生产者的⑤的 release
tail_.CAS(relaxed)                // ③ CAS 提供原子独占
data = std::move                  // ④ 普通读
slot.data.~T()                    // ⑤ 析构
slot.sequence.store(release)      // ⑥ 发布给生产者
```

**为什么 head_/tail_ 可以用 relaxed？**  
因为真正的同步由 `slot.sequence` 的 acquire/release 对完成。
head_ 和 tail_ 只是位置分配计数器，它们的值变化通过 sequence 间接传递。

### 5.2 SegmentedMPMCQueue

```cpp
// push
enqueue_pos_.fetch_add(release)   // ① 分配 ticket，release 保证其他线程看见
tail_block_.load(acquire)         // ② 看到最新的 tail
block->next.load(acquire)         // ③ 看到前次链接
block->next.CAS(release)         // ④ 发布新块
tail_block_.store(release)       // ⑤ 更新 hint（尽力）
slot.sequence.load(acquire)      // ⑥ 等待空闲
placement new                     // ⑦ 写数据
slot.sequence.store(release)     // ⑧ 发布给消费者

// pop
dequeue_pos_.CAS(acq_rel)        // ① 抢票 + 序化
enqueue_pos_.load(acquire)       // ② 看到最新产出
head_block_.load(acquire)        // ③ 看到最新消费进度
block->next.load(acquire)        // ④ 遍历
slot.sequence.load(acquire)      // ⑤ 等待生产者
data = std::move                  // ⑥ 读数据
slot.sequence.store(relaxed)     // ⑦ 标记消费（析构专用）
```

### 5.3 为什么 fetch_add 使用 release 而非 relaxed？

客户端必须通过 `enqueue_pos_.load(acquire)` 可靠地看到条目存在：

```cpp
Producer:                              Consumer:
  enqueue_pos_.fetch_add(release) ──sync──→ enqueue_pos_.load(acquire)
  slot.sequence.store(release)             slot.sequence.load(acquire)
```

若 fetch_add 用 relaxed，消费者可能持续读到旧的 enqueue_pos_ 值，
导致频繁的空队列误判（性能退化而非正确性问题）。

---

## 7. 性能模型

### 6.1 原子操作开销（LLVM Clang++ 22 / x86-64）

| 操作 | 延迟（大致） | 备注 |
|------|------------|------|
| relaxed load | ~1-2 ns | 编译器优化掉的可能为 0 |
| acquire load | ~1-2 ns | x86 上等同于 relaxed |
| release store | ~1-2 ns | x86 上等同于 relaxed |
| CAS (strong) | ~15-30 ns | `lock cmpxchg` |
| fetch_add | ~15-30 ns | `lock xadd` |
| CAS (weak) | ~10-20 ns | `lock cmpxchg` + 重试 |

### 6.2 单次 push/pop 的原子操作个数

| 队列 | push | pop | 备注 |
|------|------|-----|------|
| SPSCQueue | 1 load + 1 store | 1 load + 1 store | 无 CAS |
| SPMCQueue | 1 fetch_add + 1 store | 1 load + 条件 | 有 generational check |
| **BoundedMPMCQueue** | **1 CAS + 2 load + 1 store** | **1 CAS + 2 load + 1 store** | 路径确定 |
| **SegmentedMPMCQueue** | **1 fetch_add + 遍历 + 1 store** | **1 CAS + 遍历 + 1 store** | 遍历=0（均摊） |

### 6.3 缓存行乒乓

| 场景 | 竞争原子 | 后果 |
|------|---------|------|
| 1P/1C | head_ / tail_ 分离 | 无乒乓（不同 cache line） |
| N 生产者 | head_ CAS | CAS 竞争，约 O(N) 退化 |
| N 消费者 | tail_ CAS | 同理 |
| Segmented tail hint | CAS（尽力） | 仅块分配时，频次低 |

### 6.4 百万并发模拟

BoundedMPMCQueue（容量 65536）在 100 万次 push 下的预期性能：

| 线程模型 | 总原子操作 | 预计耗时（x86-64 3GHz） |
|---------|-----------|----------------------|
| 1P/1C | 2M CAS + 4M load/store | ~30-50 ms |
| 4P/4C | 同上（CAS 竞争加剧） | ~50-100 ms |
| 16P/16C | 同上 | ~100-200 ms |

SegmentedMPMCQueue 额外 + 约 BlockSize 分之一的开销用于块遍历。

---

## 8. 使用指南

### 8.1 头文件包含

```cpp
#include "utils/queue/BoundedMPMCQueue.hpp"
#include "utils/queue/SegmentedMPMCQueue.hpp"
```

### 8.2 BoundedMPMCQueue 基本用法

```cpp
BoundedMPMCQueue<int, 4096> queue;

// 生产者
int value = compute();
if (!queue.try_push(value)) {
    // 队列满，数据被丢弃
    log_dropped();
}

// 消费者
int result;
if (queue.try_pop(result)) {
    process(result);
}
```

### 8.3 SegmentedMPMCQueue 基本用法

```cpp
SegmentedMPMCQueue<LogEntry, 1024> queue;

// 生产者（永不丢数据）
queue.try_push(LogEntry{"INFO", "user logged in"});

// 消费者
LogEntry entry;
while (queue.try_pop(entry)) {
    write_to_disk(entry);
}
```

### 8.4 多线程流水线模式

```cpp
std::vector<std::thread> producers;
for (int t = 0; t < 4; ++t) {
    producers.emplace_back([&, t] {
        for (auto& item : my_work(t)) {
            while (!queue.try_push(item)) {}    // 自旋等待（非阻塞）
        }
    });
}

std::atomic<int> done_count{0};
std::vector<std::thread> consumers;
for (int t = 0; t < 4; ++t) {
    consumers.emplace_back([&] {
        int val;
        while (done_count.load() < TOTAL_WORK) {
            if (queue.try_pop(val)) {
                process(val);
            } else {
                std::this_thread::yield();
            }
        }
    });
}
```

### 8.5 选择合适的队列

| 场景 | 推荐 | 原因 |
|------|------|------|
| 性能监控/探针 | BoundedMPMCQueue | 溢出可接受，零分配 |
| 短暂的数据突发 | BoundedMPMCQueue | 容量足够容纳突发 |
| 系统日志 | SegmentedMPMCQueue | 不能丢日志 |
| 网络 IO 事件流 | SegmentedMPMCQueue | 不确定负载，需自动扩容 |
| 多线程算法任务分发 | BoundedMPMCQueue | 低延迟、确定性能 |
| 任务队列（工作窃取） | BoundedMPMCQueue | 配合 backpressure 协议 |

---

## 9. 百万并发注意事项

### 8.1 CAS 竞争退火

高并发下 CAS 竞争加剧，`compare_exchange_weak` 的虚假失败率上升。

**缓解措施**：
- 增大 BoundedMPMCQueue 容量（减少 prod 阻塞导致的重试）
- 增大 SegmentedMPMCQueue 的 BlockSize（减少块分配 CAS 频率）
- 消费者端使用批量 pop 模式（API 暂未提供，可封裝 loop）

### 8.2 内存屏障成本

ARM/ARM64 上 acquire/release 可能生成 `dmb` 指令（~40-80 周期延迟）。
相比 x86 上 acquire/release 为编译期屏障（零指令开销），ARM 上真实开销不可忽略。

若考虑 ARM 部署，可评估：
- 是否有更宽松的序化需求
- 是否可降低某些路径的序化等级

### 8.3 Segmented 队列内存增长

当生产持续快于消费时，SegmentedMPMCQueue 的块链表无限增长。

**实践建议**：
```cpp
// 监控队列深度
while (true) {
    auto depth = queue.size();
    if (depth > MAX_SAFE_DEPTH) {
        // 触發降级：采样丢弃、临时落盘、或警报
        trigger_backpressure();
    }
    std::this_thread::sleep_for(1s);
}
```

### 8.4 伪共享预防

两个队列均已将共享的原子变量放置在独立的 cache line 上。
但以下**外部代码**仍需注意：

```cpp
// BAD —— 相邻的原子变量在同一个 cache line
struct alignas(64) Bad {
    std::atomic<int> a;   // 生产者更新
    std::atomic<int> b;   // 消费者更新（与 a 冲突！）
};
```

### 8.5 NUMA 亲和性

多插槽服务器上，块分配在老节点、消费在新节点可能导致远端内存访问。

```cpp
// 思路：绑定生产/消费者线程到同一 NUMA 节点
// （平台相关 API，非队列库职责）
numa_run_on_node(node_id);
```

---

## 10. 与现有队列对比

所有队列实现共享 `IQueue<T>` 虚接口（`src/common/utils/queue/IQueue.h`），并通过
`QueueType<T>` 概念约束泛型消费者。参见第 4 节。

### 10.1 SPSCQueue

```
src/common/utils/queue/SPSCQueue.hpp — 单生产者单消费者

特点：无 CAS，纯 load/store，极致简单
限制：仅 1P/1C
适用：Observer 事件传递
```

### 10.2 SPMCQueue

```
src/common/utils/queue/SPMCQueue.hpp — 单生产者多消费者

特点：generational slot，消费者可以自由读取
限制：仅 1P/N 消费者
适用：日志后台写入（1 writer）、多 reader 观察
```

### 10.3 家族速查

```
                       ┌─────────┬─────────┬─────────┬──────────┐
                       │ SPSC    │ SPMC    │ MPMC    │ IQueue   │
├──────────────────────┼─────────┼─────────┼─────────┼──────────┤
│ 任意生产者            │    ❌   │    ❌   │    ✅   │   ✅     │
│ 任意消费者            │    ❌   │    ✅   │    ✅   │   ✅     │
│ 无 CAS              │    ✅   │    ❌   │    ❌   │   N/A    │
│ 溢出丢弃             │    ✅   │    ✅   │    ✅   │   N/A    │
│ 动态扩容             │    ❌   │    ❌   │    ✅*  │   N/A    │
│ 统一虚接口            │    ✅   │    ✅   │    ✅   │   —      │
│ 近似最小延迟          │    1    │    2    │    2    │   3      │
└──────────────────────┴─────────┴─────────┴─────────┴──────────┘
  * SegmentedMPMCQueue only

所有队列均通过 `QueueAdaptor<T, ConcreteQueue>` 适配为 `IQueue<T>` 多态接口，
可在运行时选择后端队列类型。
```

---

## 11. FAQ / 常见陷阱

### Q1: push 返回 false，数据丢了吗？

**BoundedMPMCQueue**：是的。满时直接返回 false，数据不会被写入。
应用层需判断是否重试或丢弃。

**SegmentedMPMCQueue**：不会。push 永远不会因满返回 false。
OOM 时抛 `std::bad_alloc`。

### Q2: 可以在中断/信号处理函数中调用 push 吗？

**不可以。** 尽管队列本身无锁，但调用方如有锁依赖（如堆分配器锁）
或在临界区中调用，仍可能死锁。Segmented 的 `new Block` 和
Bounded 已经确定不分配内存，相对安全。

建议：在中断上下文中只使用 `BoundedMPMCQueue` 的 `try_push`，且确保
中断未被堆分配器锁阻塞。

### Q3: size() 返回的值可靠吗？

不精确。`size()` 是两个原子 load 的快照——在 load(acquire) 的
瞬间和返回值的瞬间之间，其他线程可能已改变了队列状态。

用于判断"队列大概有多深"是可以的。用于条件判断时应使用
`try_push()` / `try_pop()` 的返回值。

### Q4: 为什么要求 T 是 nothrow 的？

两个要求：
```
static_assert(std::is_nothrow_destructible_v<T>);
static_assert(std::is_nothrow_move_assignable_v<T>);
```

如果在 `try_push`/`try_pop` 中 T 的构造或移动抛异常，队列的状态会不一致
（槽位被标记为"已写入"但数据不完整，或数据被搬走但未析构）。

如果 T 确实可能抛异常，用 `std::optional<T>` 或 `std::unique_ptr<T>` 包装。

### Q5: 可以拷贝队列吗？

不可以。两个队列的拷贝构造函数和拷贝赋值均被 `= delete`。
如果需要复制，显式 drain 一个队列再 `try_push` 到另一个。

### Q6: 两个线程同时 try_push 同一个元素安全吗？

安全。队列对元素本身不做任何假设——它只移动值。但外部必须
确保元素在 push 期间不被并发修改。

### Q7: 性能最大化建议

1. **容量对齐**：Bounded 容量设为 2 的幂（已是编译期约束）
2. **块大小**：Segmented 的 BlockSize 设大（4096）以减少块分配
3. **避免 size()**：在高频路径上调用 size() 会 load acquire，
   虽轻但非零。用 push/pop 返回值判断状态
4. **批量操作**：在消费者端 loop pop 以摊还循环开销
5. **affinity 绑定**：生产者和消费者绑定到同一 L3 cache 域

---

## 参考

- Dmitry Vyukov, ["Bounded MPMC queue"](https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue) (2008)
- C++ Standard, [§31.8 - Atomics](http://eel.is/c++draft/atomics)
- LLVM libc++ atomic implementation, [`<atomic>`](https://github.com/llvm/llvm-project/blob/main/libcxx/include/atomic)
- WA-DSF论文, "Practical lock-free data structures" (2012)
