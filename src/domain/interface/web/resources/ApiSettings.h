#pragma once
#include <string>

class BesqContext;
class Json; // common/io/json.h — forward decl only, see ApiSettings.cpp

/// GET/PUT /api/settings. `lang`, `log_level`, `log_console`,
/// `log_console_level` are writable at runtime; `gui_host`/`gui_port` are
/// startup-only (the server is already bound).
struct ApiSettings {
    static std::string handle_get(const BesqContext& ctx);
    /// Body must be a JSON object with the fields to update.
    static std::string handle_put(BesqContext& ctx, const Json& body);
};
