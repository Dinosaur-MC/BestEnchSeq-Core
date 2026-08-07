#include "StaticFileServer.h"
#include <algorithm>
#include <cstdint>
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

/// uintmax_t → 小写十六进制。
std::string hex_of(std::uintmax_t v) {
    if (v == 0) return "0";
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    while (v != 0) {
        out.push_back(kHex[v & 0xF]);
        v >>= 4;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

/// ETag = "hex(size)-hex(stamp)"：稳定、单调（size/mtime 变化即变化）。
std::string etag_for(std::uintmax_t size, std::uintmax_t stamp) {
    return "\"" + hex_of(size) + "-" + hex_of(stamp) + "\"";
}

/// FNV-1a 64 位内容哈希（确定性，跨进程稳定）。
std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

/// raw rel 是否含编码斜杠（%2F / %2f，大小写不敏感）。解码后会变成路径分隔符，
/// 与字面 `/a/b` 别名；在解码前直接拒绝。
bool has_encoded_slash(std::string_view s) {
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if (s[i] != '%') continue;
        if ((s[i + 1] | 0x20) == '2' && (s[i + 2] | 0x20) == 'f') return true;
    }
    return false;
}

/// If-None-Match 匹配：逗号分隔列表、忽略空白与弱前缀 W/；"*" 匹配任意存在资源。
bool etag_matches(std::string_view inm, const std::string& etag) {
    if (inm.empty()) return false;
    size_t pos = 0;
    for (;;) {
        size_t comma = inm.find(',', pos);
        std::string_view item = inm.substr(
            pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        auto b = item.find_first_not_of(" \t");
        if (b != std::string_view::npos) {
            auto e = item.find_last_not_of(" \t");
            std::string_view v = item.substr(b, e - b + 1);
            if (v == "*") return true;
            if (v.size() >= 2 && (v[0] == 'W' || v[0] == 'w') && v[1] == '/')
                v.remove_prefix(2);
            if (v == etag) return true;
        }
        if (comma == std::string_view::npos) return false;
        pos = comma + 1;
    }
}

#ifdef _WIN32
/// ASCII 小写；非字母原样（避免 `| 0x20` 把 '\\' 等非字母映射错位）。
char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}
#endif

/// 前缀比较：Windows 大小写不敏感（挂载根 vs weakly_canonical 结果可能大小写不同），
/// POSIX 精确。
bool prefix_eq(std::string_view a, std::string_view b) {
    if (a.size() < b.size()) return false;
#ifdef _WIN32
    for (size_t i = 0; i < b.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
#else
    return a.compare(0, b.size(), b) == 0;
#endif
}

/// 全串判等（Windows 大小写不敏感）。
bool str_eq(std::string_view a, std::string_view b) {
    return a.size() == b.size() && prefix_eq(a, b);
}

/// percent-decode 并校验相对路径：拒绝 `..`、空段（leading '/' 除外）、
/// 含 `\` 或 NUL 的段。返回规范化相对路径（leading '/' 保留）；非法 → ""。
std::string sanitize_relative(std::string_view rel) {
    if (has_encoded_slash(rel)) return "";  // %2F 不再归一化为路径分隔符
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
/// Windows：挂载根与 weakly_canonical 结果的盘符/大小写可能不一致，比较不敏感；
/// POSIX 保持敏感。
bool under_dir(const std::filesystem::path& canon, const std::filesystem::path& root) {
    std::string c = canon.string();
    std::string r = root.string();
    if (str_eq(c, r)) return true;
    if (c.size() <= r.size()) return false;
    if (!prefix_eq(c, r)) return false;
    char sep = c[r.size()];
    return sep == '/' || sep == '\\';
}

/// 从磁盘根解析 key（已通过 sanitize_relative 校验）；目录请求追加 index.html。
/// 穿越防护第二层：weakly_canonical + 前缀包含校验（防符号链接逃逸）。
/// ETag = size + mtime；If-None-Match 匹配（弱比较）→ 304 空 body，跳过文件读取。
HttpResponse serve_disk(const std::filesystem::path& root, const std::string& key,
                        Method method, std::string_view if_none_match) {
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
        return serve_disk(root, key + "/index.html", method, if_none_match);
    }
    if (!std::filesystem::is_regular_file(canon, sec))
        return HttpResponse::not_found();

    // ETag：size + mtime（weakly_canonical 后即可取，先算再决定是否读文件）。
    // 元数据查询失败 → 无 ETag（退化为无缓存协商，仍 200 全量）。
    std::string etag;
    {
        std::error_code fec, tec;
        std::uintmax_t size = std::filesystem::file_size(canon, fec);
        auto mtime = std::filesystem::last_write_time(canon, tec);
        if (!fec && !tec)
            etag = etag_for(size,
                            static_cast<std::uintmax_t>(mtime.time_since_epoch().count()));
    }
    if (!etag.empty() && etag_matches(if_none_match, etag)) {
        HttpResponse r;
        r.status = 304;
        r.reason = "Not Modified";
        r.content_type = mime_for(ext_of(key));
        r.headers.emplace_back("ETag", etag);
        return r;
    }

    std::ifstream f(canon, std::ios::binary);
    if (!f) return HttpResponse::not_found();
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.content_type = mime_for(ext_of(key));
    r.body = std::move(body);
    if (!etag.empty()) r.headers.emplace_back("ETag", etag);
    if (method == Method::Head) {
        r.headers.emplace_back("Content-Length", std::to_string(r.body.size()));
        r.body.clear();
    }
    return r;
}
} // namespace

HttpResponse StaticFileServer::serve(Method method, std::string_view path,
                                     std::string_view if_none_match) const {
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
        // ETag：内容长度 + FNV-1a 内容哈希——确定性，内容不变则稳定。
        std::string etag = etag_for(it->second.content.size(), fnv1a(it->second.content));
        if (etag_matches(if_none_match, etag)) {
            HttpResponse r;
            r.status = 304;
            r.reason = "Not Modified";
            r.content_type = it->second.content_type.empty()
                ? mime_for(ext_of(key))
                : it->second.content_type;
            r.headers.emplace_back("ETag", etag);
            return r;
        }
        HttpResponse r;
        r.status = 200;
        r.reason = "OK";
        r.content_type = it->second.content_type.empty()
            ? mime_for(ext_of(key))
            : it->second.content_type;
        r.body = it->second.content;
        r.headers.emplace_back("ETag", etag);
        if (method == Method::Head) {
            r.headers.emplace_back("Content-Length", std::to_string(r.body.size()));
            r.body.clear();
        }
        return r;
    }

    // 磁盘
    if (!_root.empty()) return serve_disk(_root, key, method, if_none_match);
    return HttpResponse::not_found();
}

} // namespace web
