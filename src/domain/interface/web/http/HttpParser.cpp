#include "HttpParser.h"
#include <cstdlib>

namespace webhttp {

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

} // namespace

ParseResult HttpParser::parse(const std::string& buf, size_t& consumed, HttpRequest& out) {
    consumed = 0;
    // Header terminator.
    auto hdr_end = buf.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        // Headers not yet terminated — wait for more bytes. Cap the buffer so
        // a misbehaving client that never sends the blank line can't grow the
        // buffer unboundedly (drop the connection instead).
        if (buf.size() > 64 * 1024)
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
    out.method = request_line.substr(0, sp1);
    std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string version = request_line.substr(sp2 + 1);
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
        return ParseResult::BadRequest;

    // Split path / query.
    auto q = target.find('?');
    out.path = q == std::string::npos ? target : target.substr(0, q);
    out.query = q == std::string::npos ? "" : target.substr(q + 1);

    // ── Headers ──
    size_t pos = first_nl == std::string::npos ? 0 : first_nl + 2;
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
        long len = std::strtol(cl.c_str(), nullptr, 10);
        if (len < 0) return ParseResult::BadRequest;
        if (buf.size() < consumed + static_cast<size_t>(len))
            return ParseResult::Incomplete;
        out.body = buf.substr(consumed, static_cast<size_t>(len));
        consumed += static_cast<size_t>(len);
    }
    return ParseResult::Complete;
}

} // namespace webhttp
