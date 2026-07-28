#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// Minecraft platform edition
enum class MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};

// Operation mode (bitmask).
enum class AlgorithmMode : uint8_t {
    direct    = 1 << 1,
    inventory = 1 << 2,
};

constexpr AlgorithmMode operator|(AlgorithmMode a, AlgorithmMode b) noexcept {
    return static_cast<AlgorithmMode>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool operator&(AlgorithmMode a, AlgorithmMode b) noexcept {
    return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
}

// Namespace ID
class NSID {
  private:
    std::string _ns;      // namespace
    std::string _id;      // id
    bool _is_tag = false; // `#` prefix

  public:
    NSID() = default;
    NSID(const std::string_view &ns, const std::string_view &id);
    NSID(const char *strid) : NSID(std::string_view(strid)) {}
    NSID(const std::string_view &strid);
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>> NSID(T) = delete;

    bool empty() const noexcept { return _ns.empty() && _id.empty(); }
    bool operator==(const NSID &o) const noexcept {
        return _ns == o._ns && _id == o._id && _is_tag == o._is_tag;
    }

    bool is_tag() const { return _is_tag; }
    std::string get_ns() const { return _ns; }
    std::string get_id() const { return _id; }
    std::string str() const;
    template <typename Callable>
        requires std::is_invocable_v<Callable, std::string_view> &&
                 std::is_convertible_v<std::invoke_result_t<Callable, std::string_view>, std::string>
    std::string str(Callable transform) const {
        return transform(str());
    }

    NSID &operator=(const char *strid);
    NSID &operator=(const std::string_view &strid);
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>> NSID &operator=(T) = delete;
    auto operator<=>(const NSID &other) const { return str() <=> other.str(); }
};

template <> struct std::hash<NSID> {
    size_t operator()(const NSID &nsid) const { return std::hash<std::string>()(nsid.str()); }
};
