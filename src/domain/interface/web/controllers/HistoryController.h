#pragma once
#include "domain/interface/components/http/HttpController.h"

class BesqContext;

namespace web {

/// GET /api/history — 计算求解历史（替代已删除的 /api/logs*，计划 B Task B3）。
/// 数据源为 BesqContext::solve_history()（每 context 有界、最新在前快照）。
/// 查询语义（设计文档 §2.4）：
///   - 普通查询：GET /api/history → 最近 limit 条（默认 100，上限 200 封顶），最新在前
///   - 分页：    ?offset=N&limit=M（offset 从 0 起；next_offset = offset + 本页条数）
///   - 游标：    ?after_seq=N（只返回 seq > N 的事件；seq 单调递增，供增量拉取。
///                            作为过滤先于 offset/limit 切片——两者可叠加）
///   - 校验：    offset/limit/after_seq 非数字或负数 → 400 INVALID_FIELD
/// 响应：{"events":[...],"total":<上下文全部事件数>,"next_offset":<下一页起点>}；
/// 事件序列化 SolveHistoryEvent 全字段，type 为小写字符串
/// （"submitted"/"completed"/"failed"/"cancelled"，与 SSE 帧 type 命名一致）。
class HistoryController : public HttpController<HistoryController> {
public:
    using Self = HistoryController;

    explicit HistoryController(BesqContext& ctx) : _ctx(ctx) {}

    static constexpr auto route_defs() { return std::array{BESQ_ROUTE(Get, "/api/history", list)}; }

    Response list(const HttpRequest&);

private:
    BesqContext& _ctx;
};

} // namespace web
