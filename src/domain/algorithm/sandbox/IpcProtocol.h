#pragma once

/// @file sandbox/IpcProtocol.h
/// Wire protocol between the parent process and the besq-worker sandbox.
///
/// Frame layout (little-endian):
///   [4-byte payload length][4-byte message type][payload bytes]
///
/// Messages are request/response.  During MsgExecute the worker may also
/// stream MsgProgress / MsgSolution events before the final MsgResult.
///
/// Payloads use ByteStream serialization (the compact types already
/// implement IBinarySerializable).

#include "common/io/ByteStream.h"
#include <cstdint>
#include <vector>

namespace algorithm::ipc {

enum class MsgType : uint32_t {
    // ── Parent → worker ────────────────────────────────────────────────
    MsgGetName       = 0x0101,
    MsgGetVersion    = 0x0102,
    MsgGetMode       = 0x0103,
    MsgEvaluate      = 0x0104,
    MsgSimulate      = 0x0105,
    MsgResolve       = 0x0106,
    MsgExecute       = 0x0107,
    MsgCancel        = 0x0108,
    MsgPause         = 0x0109,
    MsgResume        = 0x010A,

    // ── Worker → parent ────────────────────────────────────────────────
    MsgResult        = 0x0201,  // request completion, payload varies by request
    MsgProgress      = 0x0202,  // { uint8 pct, uint8 status } (streamed during execute)
    MsgSolution      = 0x0203,  // EnchSolution (streamed during execute)
    MsgError         = 0x0204,  // string error message
};

constexpr size_t kHeaderSize = 8;

/// Write one frame to fd.  Returns false on write error / truncation.
bool write_frame(int fd, MsgType type, const std::vector<uint8_t> &payload);

/// Read one frame from fd (blocking).  Returns false on EOF or protocol error.
bool read_frame(int fd, MsgType &out_type, std::vector<uint8_t> &out_payload);

// ── Payload helpers (ByteStream codecs) ────────────────────────────

/// Encode a trivially-serializable value into a frame payload.
template <typename T>
std::vector<uint8_t> encode_value(const T &v) {
    ByteStreamWriter w;
    w << v;
    return std::move(w).take();
}

/// Encode any IBinarySerializable object into a frame payload.
template <typename T>
std::vector<uint8_t> encode(const T &obj) {
    ByteStreamWriter w;
    obj.serialize(w);
    return std::move(w).take();
}

/// Decode a frame payload into an IBinarySerializable object.
template <typename T>
bool decode(const std::vector<uint8_t> &payload, T &obj) {
    ByteStreamReader r(payload);
    obj.deserialize(r);
    return r.ok();
}

} // namespace algorithm::ipc
