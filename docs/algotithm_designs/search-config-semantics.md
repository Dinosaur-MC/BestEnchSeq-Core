# SearchConfig 字段语义（两级契约）

> 状态：定案（2026-08-02，用户定夺"两级语义契约 + 文档"）
> 范围：`src/domain/algorithm/types/ConfigTypes.h`（注释分层）、本文档、CLI help 文案
> 关联：`.temp/issues/audit-fix-backlog.md` #6（`--max-threads` 只对 dp_merge 生效 / SearchConfig 字段策略间不一致）

---

## 1. 背景与问题

审计发现 `SearchConfig` 各字段在 5 个策略（astar / dfs 已移为插件、dp_merge / bb_dp / hamming 内置）中实际遵守情况不一致：

| SearchConfig 字段 | astar | dfs | dp_merge | bb_dp | hamming | CLI flag |
|---|---|---|---|---|---|---|
| `max_search_time` | ✓ | ✓ | ✓（Executor 超时） | ✓（Executor 超时） | ✗（O(n) 贪心，本就快） | `--max-time` |
| `max_solutions` | ✓ | ✓ | ✗（只返回 1 最优解） | ✗ | ✗ | `--solutions` |
| `initial_bound` | ✓ | ✓ | ✗ | ✓ | ✗ | — |
| `max_depth` | ✗ | ✓ | ✗ | ✗ | ✗ | — |
| `memory_mb` | ✓ | ✗ | ✗ | ✗ | ✗ | `--memory` |
| `max_threads` | ✗ | ✗ | ✓ | ✓ | ✗ | `--max-threads` |
| `max_step_cost` | ✗ | ✗ | ✗ | ✓ | ✗ | — |
| `beam_width` | ✗ | ✗ | ✗ | ✓ | ✗ | — |

`--max-threads` 对单线程策略（hamming，及以插件加载的 astar / dfs）是 no-op，此前没有任何说明。

## 2. 决策（用户定夺，2026-08-02）

**两级语义契约 + 文档，零求解行为改动。**

- **通用字段（UNIVERSAL）**：所有策略都应遵守；无法有意义的遵守处，退化为"天然满足 / 较慢的预热"，绝不影响正确性。
- **算法特有字段（ALGORITHM-SPECIFIC）**：仅命名策略遵守，其余策略 no-op；CLI help 与文档明确标注。

> 明确不采用"全部字段所有策略统一实现"（改算法行为、成本高，与"算法域功能不动"约束冲突），也不采用"纯文档"（字段级契约应落在 `ConfigTypes.h` 源码注释作为唯一事实源）。

## 3. 通用字段契约

| 字段 | 默认 | 语义 | 遵守策略 | 例外说明 |
|---|---|---|---|---|
| `max_search_time` | 180 s | 全局搜索时间预算 | **Executor 级对所有策略生效**（`AlgorithmExecutor.cpp:188` 启动 timeout watcher）；AStar/DFS 插件另在热循环内自查 | hamming 为 O(n) 贪心构建、极快，不设内检（Executor 兜底） |
| `max_solutions` | 0（不限） | 最多返回 N 个解 | AStar（`plugins/astar/AStarAlgorithm.cpp:371`）、DFS（`plugins/dfs/DFSAlgorithm.cpp:257`） | bb_dp / dp_merge / hamming 只产 1 个最优解，对任意 N≥1 天然满足；N>1 时无更多解可给——文档标注，非 bug |
| `initial_bound` | `INT32_MAX` | warm-start 上界，成本已超此值的分支剪枝 | AStar（`plugins/astar/AStarAlgorithm.cpp:214`）、DFS（`plugins/dfs/DFSAlgorithm.cpp:144`）、bb_dp（`BBDpAlgorithm.cpp:552`） | dp_merge / hamming 忽略 → 仅预热更慢，不影响正确性 |

### 通用字段的合规语义

- **不许**：因无法遵守某通用字段而产出错误解 / 漏解。
- **允许**：单解策略对 `max_solutions>1` 给出单解；warm-start 类提示被忽略。

## 4. 算法特有字段（no-op 标注）

| 字段 | 默认 | 语义 | 遵守策略 | 其余策略 |
|---|---|---|---|---|
| `max_depth` | 0 | 最大递归/栈深度 | DFS（`plugins/dfs/DFSAlgorithm.cpp:246`） | no-op |
| `memory_mb` | 0（A* 内 fallback 2048） | 开放/关闭集内存预算（MB） | AStar（`plugins/astar/AStarAlgorithm.cpp:183`） | no-op |
| `max_threads` | 0 = `hardware_concurrency` | 线程池并发 | bb_dp（`BBDpAlgorithm.cpp:515`）、dp_merge（`DPMergeAlgorithm.cpp:329`） | 单线程策略（hamming；astar / dfs 插件）no-op |
| `max_step_cost` | 39 | 每步铁砧成本上限（vanilla Too-Expensive=39），SOFT 约束 | bb_dp | no-op |
| `beam_width` | 0（精确） | Pareto 前沿束宽 | bb_dp | no-op |

> `max_step_cost`/`beam_width` 当前无 CLI 入口，仅程序性设置（benchmark / 插件 / 默认值）。

## 4b. `extra` map —— 完全非通用配置的逃生舱（2026-08-02 追加）

为**完全非通用、不值得占类型字段**的算法专用旋钮提供通用通道：`SearchConfig::extra`（`std::map<std::string,std::string>`）。

- **键命名**：策略自有前缀（如 `bb_dp.chunk_bits`、`idastar.threshold`），值由拥有该键的策略自行解析为字符串；其它策略忽略整个 map。
- **类型**：`std::map`（有序）保证序列化确定性 → checkpoint CRC 稳定。
- **序列化**：count + (key,value) 长度前缀对，追加在 `SearchConfig` 末尾。**二进制布局变化 → checkpoint `FILE_VERSION` 2→3**（旧文件被干净拒绝）。
- **CLI**：`--algo-opt key=value[,key=value]`（与 `--config` 分离——后者保持严格 forge 键校验；`--algo-opt` 只校验 key=value 形状，键归属由策略定义）。`--algo-opt` 值为任意字符串。
- **附加式**：现有算法特有类型字段（`max_depth`/`memory_mb`/`max_threads`/`max_step_cost`/`beam_width`）**不迁移**，保留类型安全；`extra` 面向未来新增旋钮 / 插件私有配置。
- 内置策略当前不消费 `extra`（各自旋钮已有类型字段）；插件与后续策略即插即用。

## 5. CLI 映射与 help

| CLI | SearchConfig | 适用范围文案 |
|---|---|---|
| `--max-time <s>` | `max_search_time` | 所有策略（Executor 级） |
| `--solutions <n>` | `max_solutions` | AStar/DFS（插件）生效；DP/贪心策略返回单个最优解 |
| `--memory <mb>` | `memory_mb` | AStar（插件）专用；其它策略忽略 |
| `--max-threads <n>` | `max_threads` | 并行策略（bb_dp / dp_merge）专用；单线程策略忽略 |
| `--algo-opt k=v,...` | `extra` | 算法专用逃生舱；键策略私有，值任意字符串 |

对应 i18n key：`cli.help.solutions_desc` / `cli.help.memory_desc` / `cli.help.max_time_desc` / `cli.help.max_threads_desc`（`--max-threads` 由内联字符串迁移为 i18n key）/ `cli.help.algo_opt_desc`（新增）。

## 6. 边界与序列化

- `SearchConfig::serialize/deserialize`（`ConfigTypes.h`）：既有类型字段顺序与含义不变；`extra` 追加在末尾。checkpoint `FILE_VERSION` **2→3**（`Checkpoint.h:25`），旧 checkpoint 被版本校验干净拒绝。
- 新字段若后续加入，须同步：两级归类注释 + 本文档 + serialize/deserialize + CLI（如需）+ i18n。

## 7. 验证

- 构建 + `ctest` 全量通过（62/62）——注释/文档/help/extra 改动零行为回归。
- 序列化测试：`test_search_config_roundtrip` 覆盖 `extra` 往返（含空 map）。
- CLI 测试：`test_algo_opt_wiring` 覆盖 `--algo-opt` 解析、接线、空/畸形报错、`apply_algo_opts` 功能。
- CLI 实测：`besq --help` 显示更新后的 `--max-threads`/`--solutions`/`--memory`/`--max-time`/`--algo-opt` 文案（en_US + zh_CN）。
