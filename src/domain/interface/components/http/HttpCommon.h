#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace web {

class StreamChannel;   // 流式响应帧投递通道（Connection 实现）；见 StreamChannel.h

enum class Method : uint8_t { Get, Head, Post, Put, Patch, Delete };
const char* method_name(Method m);
bool method_from(std::string_view s, Method& out);   // "GET"→Get；未知返回 false

/// percent-decode 一段 URL；非法转义原样保留。
std::string percent_decode(std::string_view s);

/// 简单 query 解析（& 分隔、= 分割、值 percent-decode；无 = 视为空值）。
struct QueryParams {
    std::unordered_map<std::string, std::string> kv;
    bool has(const std::string& k) const { return kv.count(k) != 0; }
    std::string get(const std::string& k) const {
        auto it = kv.find(k); return it == kv.end() ? "" : it->second;
    }
};
QueryParams parse_query(std::string_view qs);

/// 按扩展名（含点，小写）→ MIME；未知 → application/octet-stream。
const char* mime_for(std::string_view ext);

/// 路径参数容器（{name} 段捕获）。get 不存在时抛 std::out_of_range。
struct PathParams {
    std::vector<std::pair<std::string, std::string>> kv;
    std::string get(const std::string& name) const;
    std::string_view get_sv(const std::string& name) const;
    bool has(const std::string& name) const;
    size_t size() const { return kv.size(); }
};

struct HttpRequest {
    Method method = Method::Get;
    std::string path;            // raw（未解码；参数捕获时逐段解码）
    QueryParams query;           // 解析后
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    /// 请求所在的活跃连接（流式响应帧投递通道）；非连接上下文（如单元测试直调）为空。
    std::shared_ptr<StreamChannel> stream;
    /// 连接级语义（解析器在头解析完成后计算）：
    ///   keep_alive — HTTP/1.1 无 `Connection: close` 即 true；HTTP/1.0 需显式
    ///                 `Connection: keep-alive`。
    ///   expect_continue — `Expect: 100-continue`（大小写不敏感）且 Content-Length > 0；
    ///                      Connection 在等待 body 时据此发送 `100 Continue`。
    bool keep_alive = true;
    bool expect_continue = false;
    /// 对端 IPv4 地址（getpeername，Connection 构造时捕获）。限流 key 与访问
    /// 日志客户端 IP 字段的来源；单元测试直调/非连接上下文为空串。
    std::string remote_addr;
    /// 请求行 HTTP 版本（"HTTP/1.1"/"HTTP/1.0"，解析器填充）——访问日志请求行。
    std::string version;
    std::string header(const std::string& name) const;   // 大小写不敏感，缺省 ""
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string content_type = "application/json";
    std::vector<std::pair<std::string, std::string>> headers;   // 额外响应头（Allow/Location…）
    std::string body;
    bool is_stream = false;          // true → 流式（SSE），body 忽略
    /// HTTP/1.1 线格式；非流响应写 `Connection: <keep_alive ? keep-alive : close>`；
    /// is_stream 分支恒 keep-alive（不受参数影响）。
    std::string to_bytes(bool keep_alive = true) const;
    std::string header_value(const std::string& name) const;    // 大小写不敏感查找，缺省 ""
    static HttpResponse json(int status, const std::string& reason, const std::string& body);
    static HttpResponse created(const std::string& location, const std::string& body);  // 201+Location
    static HttpResponse accepted(const std::string& location, const std::string& body); // 202+Location
    static HttpResponse no_content();                          // 204
    static HttpResponse not_found();
    static HttpResponse method_not_allowed(const std::string& allow);  // 405+Allow
    static HttpResponse bad_request(const std::string& code, const std::string& message);
    static HttpResponse conflict(const std::string& code, const std::string& message);
    static HttpResponse internal_error(const std::string& message);
    static HttpResponse error(int status, const std::string& code, const std::string& message);
};

/// `text/event-stream` stream response. is_stream=true means the transport
/// switches to chunked streaming and ignores `body` (frame writing is wired in
/// the SSE transport task).
inline HttpResponse sse_stream_response() {
    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = "text/event-stream";
    r.is_stream = true;
    return r;
}

} // namespace web
