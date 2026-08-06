#pragma once
#include "HttpCommon.h"
#include <filesystem>
#include <map>
#include <string>

namespace web {

struct StaticResource {
    std::string content_type;
    std::string content;
};

/// 静态文件服务：前缀挂载 + 嵌入式内存表 + 可选磁盘根。
/// 查找序：嵌入式 → 磁盘。路径穿越防护 + MIME + HEAD。
class StaticFileServer {
public:
    void mount_embedded(std::string prefix, std::map<std::string, StaticResource> embedded);
    void mount_disk(std::string prefix, std::filesystem::path root);

    /// 解析 prefix 下的相对路径；GET/HEAD。非法/缺失 → 404。
    /// if_none_match：If-None-Match 请求头值（可为空）；ETag 匹配 → 304 空 body。
    HttpResponse serve(Method method, std::string_view path,
                       std::string_view if_none_match = {}) const;

    bool mounted() const { return !_prefix.empty(); }

private:
    std::string _prefix;
    std::map<std::string, StaticResource> _embedded;
    std::filesystem::path _root;
};

} // namespace web
