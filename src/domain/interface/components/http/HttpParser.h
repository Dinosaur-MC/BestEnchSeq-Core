#pragma once
#include "HttpCommon.h"
#include <cstddef>
#include <string>

namespace web {

/// Incomplete — 缺字节，等待更多数据；Complete — 完整请求；BadRequest — 请求格式
/// 非法（400）；EntityTooLarge — body 超过 kMaxBodyBytes 上限（413，与 400 区分）。
enum class ParseResult { Incomplete, Complete, BadRequest, EntityTooLarge };

/// 增量解析。Complete 时 consumed 为该请求占用字节（headers+body），out 填充。
/// keep-alive：调用方可对 buf.substr(consumed) 再解析下一个请求。
struct HttpParser {
    static ParseResult parse(const std::string& buf, size_t& consumed, HttpRequest& out);
};
ParseResult parse_incremental(const std::string& buf, size_t& consumed, HttpRequest& out);

} // namespace web
