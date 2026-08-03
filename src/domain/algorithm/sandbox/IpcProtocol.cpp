#include "domain/algorithm/sandbox/IpcProtocol.h"

#include <algorithm>
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
bool write_all(int fd, const uint8_t* data, size_t len) {
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
bool read_all(int fd, uint8_t* data, size_t len) {
    size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        int n = static_cast<int>(::_read(fd, data + off, static_cast<unsigned>(len - off)));
#else
        ssize_t n = ::read(fd, data + off, len - off);
#endif
        if (n <= 0)
            return false; // EOF or error
        off += static_cast<size_t>(n);
    }
    return true;
}

} // anonymous namespace

namespace {

/// Write a single frame (no chunking).  Returns false on error.
bool write_one(int fd, MsgType type, const std::vector<uint8_t>& payload) {
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

/// Read a single frame (no reassembly).  Returns false on EOF or protocol error.
bool read_one(int fd, MsgType& out_type, std::vector<uint8_t>& out_payload) {
    uint8_t header[kHeaderSize];
    if (!read_all(fd, header, kHeaderSize))
        return false;

    uint32_t len = 0, typ = 0;
    std::memcpy(&len, header, 4);
    std::memcpy(&typ, header + 4, 4);

    // Reject absurd single-frame sizes to bound memory (chunked messages are
    // sent as kChunkSize frames, so a normal frame never approaches this).
    constexpr uint32_t kMaxFrame = 64u * 1024 * 1024; // 64 MiB
    if (len > kMaxFrame)
        return false;

    out_type = static_cast<MsgType>(typ);
    out_payload.resize(len);
    if (len > 0 && !read_all(fd, out_payload.data(), out_payload.size()))
        return false;
    return true;
}

} // anonymous namespace

bool write_frame(int fd, MsgType type, const std::vector<uint8_t>& payload) {
    // Small payload → single frame.  Large → chunked sequence.
    if (payload.size() <= kDirectMax)
        return write_one(fd, type, payload);

    // MsgChunkedStart carries the real type + the first chunk.
    const uint32_t real_type = static_cast<uint32_t>(type);
    const size_t first = std::min(kChunkSize, payload.size());
    std::vector<uint8_t> head;
    head.reserve(sizeof(uint32_t) + first);
    const uint8_t* rt = reinterpret_cast<const uint8_t*>(&real_type);
    head.insert(head.end(), rt, rt + sizeof(uint32_t));
    head.insert(head.end(), payload.data(), payload.data() + first);
    if (!write_one(fd, MsgType::MsgChunkedStart, head))
        return false;

    size_t off = first;
    while (off < payload.size()) {
        const size_t n = std::min(kChunkSize, payload.size() - off);
        std::vector<uint8_t> chunk(payload.data() + off, payload.data() + off + n);
        if (!write_one(fd, MsgType::MsgChunkData, chunk))
            return false;
        off += n;
    }
    return write_one(fd, MsgType::MsgChunkEnd, {});
}

bool read_frame(int fd, MsgType& out_type, std::vector<uint8_t>& out_payload) {
    MsgType type;
    std::vector<uint8_t> payload;
    if (!read_one(fd, type, payload))
        return false;

    if (type != MsgType::MsgChunkedStart) {
        out_type = type;
        out_payload = std::move(payload);
        return true;
    }

    // Reassemble the chunked message.
    if (payload.size() < sizeof(uint32_t))
        return false;
    uint32_t real_type = 0;
    std::memcpy(&real_type, payload.data(), sizeof(uint32_t));
    out_type = static_cast<MsgType>(real_type);
    out_payload.assign(payload.begin() + sizeof(uint32_t), payload.end());
    for (;;) {
        if (!read_one(fd, type, payload))
            return false;
        if (type == MsgType::MsgChunkEnd)
            return true;
        if (type != MsgType::MsgChunkData)
            return false; // unexpected frame mid-transfer
        out_payload.insert(out_payload.end(), payload.begin(), payload.end());
    }
}

// ── AlgorithmOutput codec ──────────────────────────────────────────────

std::vector<uint8_t> encode_algorithm_output(const AlgorithmOutput& out) {
    ByteStreamWriter w;
    w << out.algorithm_name << out.algorithm_version;
    w << static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(out.created_at.time_since_epoch()).count());
    w << static_cast<int64_t>(out.computation_time.count());
    w << static_cast<uint32_t>(out.solutions.size());
    for (const auto& s : out.solutions)
        s.serialize(w);
    w << static_cast<uint64_t>(out.task_id);
    w << out.final_item;
    w << out.is_valid;
    return std::move(w).take();
}

bool decode_algorithm_output(const std::vector<uint8_t>& bytes, AlgorithmOutput& out) {
    ByteStreamReader r(bytes);
    r >> out.algorithm_name >> out.algorithm_version;
    int64_t created_ms = 0;
    r >> created_ms;
    int64_t comp_ms = 0;
    r >> comp_ms;
    uint32_t n = 0;
    r >> n;
    if (!r.ok())
        return false;
    out.created_at = std::chrono::system_clock::time_point(std::chrono::milliseconds(created_ms));
    out.computation_time = std::chrono::milliseconds(comp_ms);
    out.solutions.resize(n);
    for (auto& s : out.solutions)
        s.deserialize(r);
    uint64_t task_id = 0;
    r >> task_id;
    out.task_id = static_cast<size_t>(task_id);
    r >> out.final_item >> out.is_valid;
    return r.ok();
}

} // namespace algorithm::ipc
