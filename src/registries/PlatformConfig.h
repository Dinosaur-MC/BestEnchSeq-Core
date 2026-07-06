#pragma once
#include "types/common.h"

namespace platform {

class Config {
public:
    static Config& get_instance() {
        static Config instance;
        return instance;
    }

    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void set_active(MCE type) { active_platform_ = type; }
    MCE get_active() const { return active_platform_; }

private:
    MCE active_platform_ = MCE::Java;
};

inline MCE get_active_platform() {
    return Config::get_instance().get_active();
}

} // namespace platform
