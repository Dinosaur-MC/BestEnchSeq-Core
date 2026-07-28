# 序列化模块（`src/domain/algorithm/serialization/`）

## 设计目的

为算法层提供状态序列化能力，实现长时间搜索（A\* / IDA\* 等）的 checkpoint 功能。搜索过程中可暂停并将完整状态写出到文件，之后从该文件恢复并继续搜索。

## 架构

```
AlgorithmExecutor (调用方)
        │  serialize_state() / restore_state()
        ▼
IAlgorithmSerializer (基类)          ← 文件总装 / 校验 / 编排
  serialize()                         非虚：写文件首部 → Input 段 → 算法段
  deserialize()                       非虚：读文件首部 → CRC 校验 → 派发算法段
        │
        ▼
AStarStateSerializer (算法专属)       ← 算法专有段转换
  _serialize_state()                  ItemPool / StepPool / OpenHeap / BestG / Scalars
  _deserialize_state()                └ 解析段头 → 按 section tag 路由 → 恢复
```

### 职责分层

| 层 | 职责 | 文件 |
|---|---|---|
| **基类** | 文件首部读写、CRC 校验、Input 段处理、文件总装 | `IAlgorithmSerializer.h/.cpp` |
| **共享原语** | checkpoint 文件格式（MetaHeader / SectionHeader / Section / Checkpoint） | `Checkpoint.h/.cpp` |
| **算法专属** | 专有数据段的转换（算法内部成员 ↔ 二进制 payload），tag 常量由各算法自行定义 | `strategies/astar/AStarStateSerializer.*` |

### IAlgorithm 集成

```cpp
class IAlgorithm {
    // 返回关联的序列化器，nullptr 表示不支持
    virtual IAlgorithmSerializer* get_serializer() noexcept { return nullptr; }
    virtual const IAlgorithmSerializer* get_serializer() const noexcept { return nullptr; }

    // 初始化，在 execute() 前总被调用一次。算法在此检查 ctx.is_restored()
    // 以区分首次运行和恢复运行。
    virtual void init(const AlgorithmInput &input, const ExecutionContext &ctx);
};
```

AlgorithmExecutor 调用路径：

```
// 正常启动
start(input) → algo.init(input, ctx) → algo.execute(input, ctx)

// 从 checkpoint 恢复
start(checkpoint_data)
  → ser->deserialize(*algo, input, data)  // 恢复算法状态
  → ctx.set_restored(true)
  → algo.init(input, ctx)                 // 算法检测 ctx.is_restored()，跳过预分配
  → algo.execute(input, ctx)              // 统一搜索循环
```

AlgorithmInput 由 Executor 持有，序列化/反序列化时作为显式参数传递，不经过 IAlgorithm 接口。

约束：
- `serialize_state()` 仅当 executor 处于 **Paused** 状态时可用
- `restore_state()` 仅当 executor 处于 **Idle** 状态时可用

### ExecutionContext 新增

```cpp
class ExecutionContext {
public:
    bool is_restored() const noexcept;
    void set_restored(bool v) noexcept;
};
```

算法首次运行时 `ctx.is_restored() == false`；从 checkpoint 恢复后 `ctx.is_restored() == true`。算法在 `init()` 中据此判断是否需要重新分配 pool / heap 等数据结构。

### 算法适配示例（AStar）

```
旧方案：
  execute() 600 行 = init(50) + search(550)
  _restore_and_execute() 600 行 = restore_init(50) + search(550)  // 完全重复

新方案：
  init()           ：设置 config/target/scratch buffers
                      若 ctx.is_restored() 则跳过 pool/heap 重置
  execute()        ：种子 + guard + greedy bound + search loop + exit diagnostics
                      首次/恢复走不同分支但共享搜索循环
```

## 文件格式

### 文件首部（固定字段 29 字节 + 变长 tag）

```
┌──────────┬──────────┬──────────┬──────────────┐
│ magic(4) │ ver(2)   │ flags(2) │ num_sects(4) │
├──────────┴──────────┴──────────┴──────────────┤
│ timestamp(8)  unix ms                         │
├─────────────┬────────────┬────────────────────┤
│ crc_code(7) │ alg_ver(2) │ tag... (string)    │
└─────────────┴────────────┴────────────────────┘
```

- `magic` = `0x51534542` (`"BESQ"` LE) — 文件类型识别
- `ver` = 1 — 文件格式版本
- `crc_code` — 7 字节 CRC-56，由 `Checkpoint::finalize()` 计算，`Checkpoint::verify()` 校验
  - 旧 checkpoint（crc_code 全零）自动跳过校验，向后兼容
- `tag` — 算法标识（如 `"astar"`），来源于 `IAlgorithm::name()`，纯元数据

### 段首部（16 字节）

```
┌──────────┬──────────────┬──────────────┐
│ type(4)  │ section_id(4)│ payload_len(8)│
├──────────┴──────────────┴──────────────┤
│ payload: length(payload_len) bytes     │
└────────────────────────────────────────┘
```

- `type`：高位 bit 31 = `SECTION_TYPE_ALGO`（`0x80000000`）标识算法专有段
  - 算法专有段使用 `checkpoint::make_algo_tag(raw_tag)` 构造 type
  - 读取时使用 `checkpoint::get_algo_tag(header.type)` 提取 raw tag
  - raw tag 由各算法自行定义（`AStarStateSerializer` 内部匿名 namespace 常量）
- `section_id`：INPUT 段固定为 0，算法专有段从 1 开始递增

### 文件整体布局

```
┌──────────────────────────────────────┐
│ 文件首部（MetaHeader）                │
├──────────────────────────────────────┤
│ INPUT 段  (type=SECTION_TYPE_INPUT)   │
│   → AlgorithmInput（全部输入数据）      │
├──────────────────────────────────────┤
│ 算法专有段 1~N  (type MSB=1)          │
│   → 算法内部状态 (ItemPool, heap...)   │
└──────────────────────────────────────┘
```

## 数据校验

1. Quick peek：读取前 6 字节校验 `magic` + `version`（不依赖 `sizeof` 结构体）
2. CRC-56：`Checkpoint::finalize()` 计算所有 section payload 的校验和
3. 反序列化时 `Checkpoint::verify()` 重算并比对（全零 CRC 跳过，兼容旧文件）
4. 段数量上限 `MAX_CHECKPOINT_SECTIONS`（256），防止恶意大文件
5. 所有读取循环检查 `r.ok()`，截断时立即 break

## 扩展指南

为新的算法添加序列化支持：

1. 创建 `FooStateSerializer` 继承 `IAlgorithmSerializer`
2. 实现 `_serialize_state()` — 将算法内部成员写入二进制段
3. 实现 `_deserialize_state()` — 解析段头、按 section tag 路由、恢复状态
4. 在算法构造函数中创建 serializer 实例：`_serializer = std::make_unique<FooStateSerializer>()`
5. 覆写 `get_serializer()` 返回 `_serializer.get()`
6. 在算法中添加 `init()` 覆盖，检查 `ctx.is_restored()` 决定是否跳过预分配

### Section tag 约定

算法专有段的 tag 值由各算法在匿名 namespace 中自行定义，**不放入公共头文件**：

```cpp
namespace {
    constexpr uint32_t TAG_ITEM_POOL = 1;
    constexpr uint32_t TAG_STEP_POOL = 2;
    // ...
}

// 写入时：
sect.header.type = checkpoint::make_algo_tag(TAG_ITEM_POOL);

// 读取时：
switch (checkpoint::get_algo_tag(sect.header.type)) {
    case TAG_ITEM_POOL: ... break;
}
```

### 规范建议

- 通过 `_x_export_*()` / `_x_import_*()` 访问器中转数据访问，而非直接 `friend`
- 写入使用 `checkpoint::make_algo_tag(tag)`，读取使用 `checkpoint::get_algo_tag()`
- 所有读取循环检查 `r.ok()` 以防御截断
- 各段 payload 内信息完整（自描述），不依赖上下文中其他段

## 测试

| 测试 | 内容 | 位置 |
|---|---|---|
| `test_compact_serializer` | Ench / EnchSet / Item / EnchStep / EnchSolution round-trip | `tests/domain/algorithm/test_compact_serializer.cpp` |
| `test_astar_state_serializer` | AStarStateSerializer 接口 + CRC roundtrip + tamper detection | `tests/domain/algorithm/test_astar_state_serializer.cpp` |
