#pragma once
#include "domain/interface/components/http/HttpController.h"

namespace web {

/// GET /api/fs/list?path= — list one directory (non-recursive) for the
/// directory-picker API. The requested path is resolved against the
/// server's working directory and MUST stay inside it; anything else (missing
/// target, a file, or an escape above the root) is a 400 INVALID_PATH. The
/// response is `{"path":…, "root":…, "entries":[{name,is_dir,size},…]}` with
/// directories first (see file_utils::list_directory).
class FsController : public HttpController<FsController> {
public:
    using Self = FsController;

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get, "/api/fs/list", list),
        };
    }

    Response list(const HttpRequest&);
};

} // namespace web
