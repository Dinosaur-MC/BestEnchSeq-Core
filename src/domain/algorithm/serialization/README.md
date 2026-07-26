# 序列化模块（`src/domain/algorithm/serialization/`）

## 设计目的

为算法层提供状态序列化能力，实现长时间搜索（A\* / IDA\* 等）的 checkpoint 功能。搜索过程中可暂停并将完整状态写出到文件，之后从该文件恢复并继续搜索。

## 架构

```
AlgorithmExecutor (调用方)
        │  serialize_state() / restore_state()
        ▼
IAlgorithmSerializer (基类)          ← 文件总装 / 校验 / 编排
  serialize()                         非虚：写文件首部 → 共通段 → 算法段
  deserialize()                       非虚：读文件首部 → CRC 校验 → 派发算法段
        │
        ▼
AStarStateSerializer (算法专属)       ← 算法专有段转换
  _write_algo_sections()              ItemPool / StepPool / OpenHeap / BestG / Scalars
  _read_algo_sections()               └ 解析段头 → 按 section_id 路由 → 恢复
```

### 职责分层

| 层 | 职责 | 文件 |
|---|---|---|
| **基类** | 文件首部读写、CRC 校验、共通段处理、文件总装 | `IAlgorithmSerializer.h/.cpp` |
| **共享原语** | compact 类型的二进制读写、段首部、文件首部结构 | `CompactSerializer.h/.cpp` |
| **算法专属** | 专有数据段的转换（算法内部成员 ↔ 二进制 payload） | `strategies/astar/AStarStateSerializer.*` |

### IAlgorithm 集成

```cpp
class IAlgorithm {
    // 返回关联的序列化器，nullptr 表示不支持
    virtual IAlgorithmSerializer* get_serializer() noexcept { return nullptr; }
    virtual const IAlgorithmSerializer* get_serializer() const noexcept { return nullptr; }
};
```

AlgorithmExecutor 调用路径：

```
serialize_state()  → get_serializer() → ser->serialize(*algo, _algorithm_input) → vector<uint8_t>
restore_state()    → get_serializer() → ser->deserialize(*algo, _algorithm_input, data)
```

AlgorithmInput 由 Executor 持有，序列化/反序列化时作为显式参数传递，不经过 IAlgorithm 接口。

约束：
- `serialize_state()` 仅当 executor 处于 **Paused** 状态时可用
- `restore_state()` 仅当 executor 处于 **Idle** 状态时可用

## 文件格式

### 文件首部（30 + tag_len 字节）

```
┌──────────┬──────────┬──────────┬──────────────┐
│ magic(4) │ ver(2)   │ flags(2) │ num_sects(4) │
├──────────┴──────────┴──────────┴──────────────┤
│ timestamp(8)  unix ms                         │
├─────────────┬────────────┬────────────────────┤
│ crc_code(7) │ alg_ver(2) │ tag_len(1)         │
├─────────────┴────────────┴────────────────────┤
│ tag(tag_len)  算法标识，如 "astar"              │
└───────────────────────────────────────────────┘
```

- `magic` = `0x51534542` (`"BESQ"` LE) — 文件类型识别
- `ver` = 1 — 文件格式版本（段首部不再重复）
- `crc_code` — 7 字节 CRC-56 校验，覆盖首部之后的所有数据

### 段首部（20 字节）

```
┌──────────┬───────────┬───────────────┬─────────┐
│ type(4)  │ flags(4)  │ section_id(4) │ len(8)  │
├──────────┴───────────┴───────────────┴─────────┤
│ payload: length(len) bytes                     │
└────────────────────────────────────────────────┘
```

- `type`: 高位 MSB=0 为**共通段**（所有算法共享），MSB=1 为**算法专有段**
- `section_id`: 从 1 开始递增，整个文件内唯一

### 文件整体布局

```
┌──────────────────────────────────────┐
│ 文件首部                              │
├──────────────────────────────────────┤
│ 共通段 1  (type MSB=0)                │
│   → 算法元数据（名称 + 版本）            │
├──────────────────────────────────────┤
│ 专有段 1~N  (type MSB=1)              │
│   → 算法内部状态 (ItemPool, heap...)   │
└──────────────────────────────────────┘
```

## 数据校验

1. 读取文件首部时校验 `magic` + `version`
2. 读取 `crc_code`，对后续所有数据计算 CRC-56 并比对
3. 遍历段时校验段头完整性（`type` / `section_id` / `len`）
4. 读取循环中每个元素后检查 `r.ok()`，截断时立即 break
5. 段头校验失败时 `r.skip(len)` 跳过 payload，保持读取游标对齐

## 扩展指南

为新的算法添加序列化支持：

1. 创建 `FooStateSerializer` 继承 `IAlgorithmSerializer`
2. 实现 `_write_algo_sections()` — 将算法内部成员写入二进制段
3. 实现 `_read_algo_sections()` — 解析段头、按 `section_id` 路由、恢复状态
4. 在算法构造函数中创建 serializer 实例：`_serializer = std::make_unique<FooStateSerializer>()`
5. 覆写 `get_serializer()` 返回 `_serializer.get()`

### 规范建议

- 通过 `_x_export_*()` / `_x_import_*()` 访问器中转数据访问，而非直接 `friend`
- 每段使用 `write_section_header(w, SECTION_TYPE_ALGO, section_id++, payload_size)`
- 所有读取循环检查 `r.ok()` 以防御截断
- 各段 payload 内信息完整（自描述），不依赖上下文中其他段

## 测试

| 测试 | 内容 | 位置 |
|---|---|---|
| `test_compact_serializer` | Ench / EnchSet / Item / EnchStep / EnchSolution round-trip | `tests/domain/algorithm/test_compact_serializer.cpp` |
| `test_astar_state_serializer` | AStarStateSerializer 接口 + 标识 | `tests/domain/algorithm/test_astar_state_serializer.cpp` |
