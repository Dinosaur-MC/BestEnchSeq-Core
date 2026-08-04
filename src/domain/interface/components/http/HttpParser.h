#pragma once
#include "HttpCommon.h"
#include <cstddef>
#include <string>

namespace web {

enum class ParseResult { Incomplete, Complete, BadRequest };

/// 增量解析。Complete 时 consumed 为该请求占用字节（headers+body），out 填充。
/// keep-alive：调用方可对 buf.substr(consumed) 再解析下一个请求。
struct HttpParser {
    static ParseResult parse(const std::string& buf, size_t& consumed, HttpRequest& out);
};
ParseResult parse_incremental(const std::string& buf, size_t& consumed, HttpRequest& out);

} // namespace web
