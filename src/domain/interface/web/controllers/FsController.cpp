#include "FsController.h"
#include "domain/interface/components/http/Router.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include <filesystem>
#include <string>

namespace web {

namespace {

/// Resolve `raw` (empty → the root itself; absolute or relative) against
/// `root` and verify the result is an existing directory INSIDE `root`.
/// Symlinks are resolved (weakly_canonical), so a link pointing out of the
/// root is treated as escaping. Returns false on any violation.
bool resolve_within(const std::filesystem::path& root, const std::string& raw,
                    std::filesystem::path& out) {
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    std::filesystem::path p(raw.empty() ? "." : raw);
    if (!p.is_absolute()) p = base / p;
    const std::filesystem::path target = std::filesystem::weakly_canonical(p, ec);
    if (ec) return false;
    if (!std::filesystem::is_directory(target, ec) || ec) return false;

    // `lexically_relative` is case-sensitive; on Windows the root and a
    // hand-typed path may differ in drive-letter case. Compare the leading
    // ".." markers case-insensitively so `F:\…` vs `f:\…` is not a false
    // escape (the containment itself is still a prefix check).
    std::string rel = target.lexically_relative(base).string();
#ifdef _WIN32
    for (auto& c : rel)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
    if (rel.rfind("..", 0) == 0) return false;
    out = target;
    return true;
}

} // namespace

Response FsController::list(const HttpRequest& req) {
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::current_path(ec);
    if (ec)
        throw WebHttpError(500, "INTERNAL_ERROR", "cannot resolve the working directory");

    std::filesystem::path target;
    if (!resolve_within(root, req.query.get("path"), target))
        throw WebHttpError(400, "INVALID_PATH",
                           "path must be an existing directory inside the working directory");

    Json o = Json::object();
    o["path"] = Json(target.generic_string());
    o["root"] = Json(std::filesystem::weakly_canonical(root, ec).generic_string());
    Json arr = Json::array();
    for (const auto& e : file_utils::list_directory(target)) {
        Json je = Json::object();
        je["name"] = Json(e.name);
        je["is_dir"] = Json(e.is_dir);
        je["size"] = Json(static_cast<int64_t>(e.size));
        arr.push_back(std::move(je));
    }
    o["entries"] = arr;
    return Response::json(200, "OK", o.to_string());
}

} // namespace web
