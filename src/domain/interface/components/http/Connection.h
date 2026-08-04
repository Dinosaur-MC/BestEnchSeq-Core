// src/domain/interface/components/http/Connection.h
#pragma once
#include "HttpCommon.h"
#include "HttpParser.h"
#include <functional>
#include <string>

namespace web {

/// 一条连接的运行状态机（归属某个 home loop 线程，单线程访问，无需锁）。
/// process() 每次最多推进：解析 →（缺数据时）读一块 →（完整请求则）分发 → 写一块。
/// 返回 true 表示本调用读了数据或分发了请求，false 表示 would-block/无进展。
/// 连接默认为 keep-alive：一个请求处理完后清空缓冲，等待下一条请求。
class Connection {
public:
    using Router = std::function<HttpResponse(const HttpRequest&)>;

    Connection(int fd, std::string id);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return _fd; }
    const std::string& id() const { return _id; }
    bool alive() const { return _alive; }
    /// keep-alive 连接可接受输入时即应读（对端 FIN 后不再读）。
    bool wants_read() const { return _alive && !_pending_eof; }
    /// 有积压输出待写。
    bool wants_write() const { return !_out.empty(); }

    /// 一次非阻塞推进。router 提供分发。超时由调用方（Reactor）管。
    bool process(const Router& router);
    void close();

private:
    void drain_out();            // 尽力写 _out

    int _fd;
    std::string _id;
    bool _alive = true;
    bool _pending_eof = false;   // 已读到 EOF（对端不再发数据；输出清空后关闭）
    std::string _in;             // 未消费输入缓冲
    std::string _out;            // 待写输出缓冲
    HttpParser _parser;          // 增量解析器（内部保留半请求状态）
};

} // namespace web
