# 编排域（`src/domain/orchestration/`）

跨域胶水层，负责协调算法域、业务域和接口域之间的交互。

## 目录结构

```
types/          ← Pipeline 契约（SolveRequest/Result, ManageRequest/Result, ExportRequest/Result）
pipelines/      ← 任务协调器（SolvePipeline, ManagePipeline, ExportPipeline）
components/     ← 共享适配器和格式化器（CompactAdapter, OutputFormatter, EnchSerializer）
```

## Pipeline 一览

| Pipeline | 职责 | 阶段 |
|----------|------|------|
| SolvePipeline | 锻造求解 | stage_apply() → stage_execute() → stage_recall() |
| ManagePipeline | Profile/注册表管理 | 按 Action 分派到 ProfileManager/RegistryHelper |
| ExportPipeline | 数据导出 | 按 TargetType+Format 分派到 EnchSerializer/OutputFormatter |

## 关键设计

- **Pipeline 模式**：每个 Pipeline 是带单个 `run()` 方法的纯结构体，不自注册、不虚分派
- **Profile-aware**：所有 Pipeline 接收 Profile 或 ProfileManager，不接收裸注册表

详见 `docs/domain_designs/orchestration-domain-design.md`。
