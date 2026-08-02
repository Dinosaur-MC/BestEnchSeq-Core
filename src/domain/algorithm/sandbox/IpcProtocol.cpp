#include "domain/algorithm/sandbox/IpcProtocol.h"

#include <cstring>

#if defined(_WIN32)
// Windows sandbox deferred to M2 — these POSIX helpers are Linux-first.
#include <io.h>
#else
#include <unistd.h>
#endif

namespace algorithm::ipc {

namespace {

/// Write exactly `len` bytes, retrying on partial writes.
bool write_all(int fd, const uint8_t *data, size_t len) {
    size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        int n = static_cast<int>(::_write(fd, data + off, static_cast<unsigned>(len - off)));
#else
        ssize_t n = ::write(fd, data + off, len - off);
#endif
        if (n <= 0)
            return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

/// Read exactly `len` bytes, retrying on partial reads.
bool read_all(int fd, uint8_t *data, size_t len) {
    size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        int n = static_cast<int>(::_read(fd, data + off, static_cast<unsigned>(len - off)));
#else
        ssize_t n = ::read(fd, data + off, len - off);
#endif
        if (n <= 0)
            return false;  // EOF or error
        off += static_cast<size_t>(n);
    }
    return true;
}

} // anonymous namespace

bool write_frame(int fd, MsgType type, const std::vector<uint8_t> &payload) {
    const uint32_t len = static_cast<uint32_t>(payload.size());
    const uint32_t typ = static_cast<uint32_t>(type);

    uint8_t header[kHeaderSize];
    std::memcpy(header, &len, 4);
    std::memcpy(header + 4, &typ, 4);

    if (!write_all(fd, header, kHeaderSize))
        return false;
    if (!payload.empty() && !write_all(fd, payload.data(), payload.size()))
        return false;
    return true;
}

bool read_frame(int fd, MsgType &out_type, std::vector<uint8_t> &out_payload) {
    uint8_t header[kHeaderSize];
    if (!read_all(fd, header, kHeaderSize))
        return false;

    uint32_t len = 0, typ = 0;
    std::memcpy(&len, header, 4);
    std::memcpy(&typ, header + 4, 4);

    // Reject absurd frame sizes to bound memory.
    constexpr uint32_t kMaxFrame = 64u * 1024 * 1024;  // 64 MiB
    if (len > kMaxFrame)
        return false;

    out_type = static_cast<MsgType>(typ);
    out_payload.resize(len);
    if (len > 0 && !read_all(fd, out_payload.data(), out_payload.size()))
        return false;
    return true;
}

} // namespace algorithm::ipc
