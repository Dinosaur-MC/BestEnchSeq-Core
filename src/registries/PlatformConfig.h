#pragma once
#include <cstdint>

namespace platform {

enum MCE : int8_t {
    None = 0x00,
    Java = 0x01,
    Bedrock = 0x02,
    All = 0x03,
};

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
