#pragma once
#include "HttpCommon.h"
#include <cstddef>
#include <string>

namespace webhttp {

enum class ParseResult { Incomplete, Complete, BadRequest };

/// Incremental HTTP/1.1 request parser.
///
/// Feed it a growing buffer (`buf`). On `Complete`, `consumed` is the number
/// of bytes belonging to this request (headers + body) and `out` is filled.
/// On `Incomplete`, feed more bytes. On `BadRequest`, drop the connection.
struct HttpParser {
    static ParseResult parse(const std::string& buf, size_t& consumed, HttpRequest& out);
};

} // namespace webhttp
