#include "StaticFileServer.h"
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace web {

void StaticFileServer::mount_embedded(std::string prefix,
                                      std::map<std::string, StaticResource> embedded) {
    _prefix = std::move(prefix);
    _embedded = std::move(embedded);
}

void StaticFileServer::mount_disk(std::string prefix, std::filesystem::path root) {
    _prefix = std::move(prefix);
    _root = std::move(root);
}

namespace {
/// 提取路径最后一个 '/' 之后的扩展名（含点，原样）；无扩展名 → ""。
std::string_view ext_of(std::string_view p) {
    auto dot = p.find_last_of('.');
    if (dot == std::string_view::npos) return "";
    auto slash = p.find_last_of('/');
    if (slash != std::string_view::npos && slash > dot) return "";
    return p.substr(dot);
}

/// percent-decode 并校验相对路径：拒绝 `..`、空段（leading '/' 除外）、
/// 含 `\` 或 NUL 的段。返回规范化相对路径（leading '/' 保留）；非法 → ""。
std::string sanitize_relative(std::string_view rel) {
    std::string dec = percent_decode(rel);
    if (dec.empty()) return dec;  // 目录请求，调用方补 /index.html
    if (dec[0] != '/') return ""; // 非本挂载点下的绝对段
    std::string out;
    size_t pos = 0;
    bool first = true;
    while (pos <= dec.size()) {
        size_t slash = dec.find('/', pos);
        std::string_view seg = slash == std::string::npos
            ? std::string_view(dec).substr(pos)
            : std::string_view(dec).substr(pos, slash - pos);
        if (!first && seg.empty()) return "";  // //、尾部 /、重复空段
        if (seg == "..") return "";            // 穿越
        if (seg.find('\\') != std::string_view::npos ||
            seg.find('\0') != std::string_view::npos)
            return "";
        if (!first) out += '/';
        out += seg;
        first = false;
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return out;
}

/// 前缀判断：canon 是否位于 root 目录下（含分隔符边界）。
bool under_dir(const std::filesystem::path& canon, const std::filesystem::path& root) {
    std::string c = canon.string();
    std::string r = root.string();
    if (c == r) return true;
    if (c.size() <= r.size()) return false;
    if (c.compare(0, r.size(), r) != 0) return false;
    char sep = c[r.size()];
    return sep == '/' || sep == '\\';
}

/// 从磁盘根解析 key（已通过 sanitize_relative 校验）；目录请求追加 index.html。
/// 穿越防护第二层：weakly_canonical + 前缀包含校验（防符号链接逃逸）。
HttpResponse serve_disk(const std::filesystem::path& root, const std::string& key,
                        Method method) {
    std::error_code ec;
    std::filesystem::path root_canon = std::filesystem::weakly_canonical(root, ec);
    if (ec) return HttpResponse::not_found();

    std::filesystem::path candidate = root / key.substr(1);  // 去 leading '/'，已安全
    std::filesystem::path canon = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return HttpResponse::not_found();
    if (!under_dir(canon, root_canon)) return HttpResponse::not_found();

    std::error_code sec;
    if (std::filesystem::is_directory(canon, sec)) {
        // 目录请求 → 尝试 index.html
        return serve_disk(root, key + "/index.html", method);
    }
    if (!std::filesystem::is_regular_file(canon, sec))
        return HttpResponse::not_found();

    std::ifstream f(canon, std::ios::binary);
    if (!f) return HttpResponse::not_found();
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = mime_for(ext_of(key));
    r.body = std::move(body);
    if (method == Method::Head) {
        r.headers.emplace_back("Content-Length", std::to_string(r.body.size()));
        r.body.clear();
    }
    return r;
}
} // namespace

HttpResponse StaticFileServer::serve(Method method, std::string_view path) const {
    if (method != Method::Get && method != Method::Head)
        return HttpResponse::not_found();
    if (_prefix.empty()) return HttpResponse::not_found();
    if (path.size() < _prefix.size() || path.compare(0, _prefix.size(), _prefix) != 0)
        return HttpResponse::not_found();

    std::string_view rel = path.substr(_prefix.size());
    if (!rel.empty() && rel[0] != '/')
        return HttpResponse::not_found();  // /publicfoo 等非挂载路径
    if (rel.empty() || rel == "/") rel = "/index.html";   // 目录请求 → index

    std::string key = sanitize_relative(rel);
    if (key.empty()) return HttpResponse::not_found();

    // 嵌入式优先
    auto it = _embedded.find(key);
    if (it != _embedded.end()) {
        HttpResponse r;
        r.status = 200;
        r.reason = "OK";
        r.content_type = it->second.content_type.empty()
            ? mime_for(ext_of(key))
            : it->second.content_type;
        r.body = it->second.content;
        if (method == Method::Head) {
            r.headers.emplace_back("Content-Length", std::to_string(r.body.size()));
            r.body.clear();
        }
        return r;
    }

    // 磁盘
    if (!_root.empty()) return serve_disk(_root, key, method);
    return HttpResponse::not_found();
}

} // namespace web
