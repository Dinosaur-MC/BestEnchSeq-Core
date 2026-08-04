#include "HttpParser.h"
#include <cerrno>
#include <cstdlib>

namespace web {

namespace {

void trim_cr(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

bool split_header(const std::string& line, std::string& name, std::string& value) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return false;
    name = line.substr(0, colon);
    value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    trim_cr(value);
    return true;
}

constexpr size_t kMaxHeaderBytes = 64 * 1024;   // 头缓冲上限（旧行为保留）
constexpr size_t kMaxBodyBytes   = 1 * 1024 * 1024;  // 413 语义

} // namespace

ParseResult parse_incremental(const std::string& buf, size_t& consumed, HttpRequest& out) {
    consumed = 0;
    // Header terminator.
    auto hdr_end = buf.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        // Headers not yet terminated — wait for more bytes. Cap the buffer so
        // a misbehaving client that never sends the blank line can't grow the
        // buffer unboundedly (drop the connection instead).
        if (buf.size() > kMaxHeaderBytes)
            return ParseResult::BadRequest;
        return ParseResult::Incomplete;
    }

    std::string head = buf.substr(0, hdr_end);
    consumed = hdr_end + 4;

    // ── Request line ──
    auto first_nl = head.find("\r\n");
    std::string request_line = first_nl == std::string::npos ? head : head.substr(0, first_nl);
    auto sp1 = request_line.find(' ');
    auto sp2 = sp1 == std::string::npos ? std::string::npos : request_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos)
        return ParseResult::BadRequest;
    std::string method = request_line.substr(0, sp1);
    std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string version = request_line.substr(sp2 + 1);
    if (!method_from(method, out.method))     // 未知 method → BadRequest
        return ParseResult::BadRequest;
    if (target.empty())
        return ParseResult::BadRequest;
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
        return ParseResult::BadRequest;

    // Split path / query：path 保持 raw（未解码；{param} 段由 Router::match_segments
    // 捕获时逐段 percent-decode——整路径解码会把 %2F 变成分隔符并二次解码，破坏 %2F 数据），
    // query 值走 parse_query 解码。
    auto q = target.find('?');
    out.path = q == std::string::npos ? target : target.substr(0, q);
    out.query = parse_query(q == std::string::npos ? "" : target.substr(q + 1));

    // ── Headers ──
    size_t pos = first_nl == std::string::npos ? head.size() : first_nl + 2;
    out.headers.clear();
    while (pos <= head.size()) {
        auto nl = head.find("\r\n", pos);
        std::string line = nl == std::string::npos ? head.substr(pos) : head.substr(pos, nl - pos);
        if (line.empty()) break;
        std::string name, value;
        if (!split_header(line, name, value))
            return ParseResult::BadRequest;
        out.headers.emplace_back(name, value);
        if (nl == std::string::npos) break;
        pos = nl + 2;
    }

    // ── Body ──
    auto cl = out.header("Content-Length");
    if (!cl.empty()) {
        errno = 0;
        char* end = nullptr;
        long len = std::strtol(cl.c_str(), &end, 10);
        // Reject non-numeric (no digits / trailing junk) and overflow.
        if (errno == ERANGE || end == cl.c_str() || *end != '\0')
            return ParseResult::BadRequest;
        if (len < 0) return ParseResult::BadRequest;
        if (static_cast<size_t>(len) > kMaxBodyBytes)   // 413 语义
            return ParseResult::BadRequest;
        if (buf.size() < consumed + static_cast<size_t>(len))
            return ParseResult::Incomplete;
        out.body = buf.substr(consumed, static_cast<size_t>(len));
        consumed += static_cast<size_t>(len);
    }
    return ParseResult::Complete;
}

ParseResult HttpParser::parse(const std::string& buf, size_t& consumed, HttpRequest& out) {
    return parse_incremental(buf, consumed, out);
}

} // namespace web
