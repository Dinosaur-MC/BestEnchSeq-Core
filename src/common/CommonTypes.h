#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// Minecraft platform edition
enum class MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};

// Namespace ID
class NSID {
  private:
    std::string _ns; // namespace
    std::string _id; // id

  public:
    NSID() = default;
    NSID(const std::string_view &ns, const std::string_view &id);
    NSID(const std::string_view &nsid);

    std::string get_ns() const { return _ns; }
    std::string get_id() const { return _id; }
    std::string str() const { return _id.empty() ? "" : _ns + ":" + _id; }

    NSID &operator=(const std::string_view &nsid);
    auto operator<=>(const NSID &other) const { return str() <=> other.str(); }
};

template <> struct std::hash<NSID> {
    size_t operator()(const NSID &nsid) const { return std::hash<std::string>()(nsid.str()); }
};
