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
#include "domain/algorithm/types/AlgorithmTypes.h"
#include <cstdint>
#include <vector>

namespace algorithm::ipc {

enum class MsgType : uint32_t {
    // ── Internal chunking markers — transparent to callers ─────────────
    // A large payload (> kDirectMax) is written as a sequence of frames:
    //   MsgChunkedStart  payload = [4B real type][first chunk bytes]
    //   MsgChunkData     payload = chunk bytes
    //   MsgChunkEnd      empty
    // read_frame() reassembles them back into ONE logical message, so every
    // caller gets transparent MB–GB payloads without a separate chunk protocol.
    MsgChunkedStart = 0x0000,
    MsgChunkData = 0x0001,
    MsgChunkEnd = 0x0002,

    // ── Parent → worker ────────────────────────────────────────────────
    MsgGetName = 0x0101,
    MsgGetVersion = 0x0102,
    MsgGetMode = 0x0103,
    MsgEvaluate = 0x0104,
    MsgSimulate = 0x0105,
    MsgIsSerializable = 0x0106, // worker replies bool (executor has a serializer?)
    MsgRun = 0x0107,            // payload = AlgorithmInput (fresh solve)
    MsgResumeRun = 0x010C,      // payload = opaque checkpoint blob (resume)
    MsgCancel = 0x0108,
    MsgPause = 0x0109,
    MsgResume = 0x010A,
    MsgSerializeState = 0x010B, // reply MsgResult = full opaque checkpoint blob

    // ── Worker → parent ────────────────────────────────────────────────
    MsgResult = 0x0201,   // request completion, payload varies by request
    MsgProgress = 0x0202, // { uint8 pct, uint8 status } (streamed during execute)
    MsgSolution = 0x0203, // EnchSolution steps (streamed during execute)
    MsgError = 0x0204,    // string error message
};

constexpr size_t kHeaderSize = 8;

/// Transparent large-payload chunking.  Payloads > kDirectMax are streamed as
/// kChunkSize frames and reassembled by read_frame(), so MB–GB checkpoints
/// cross the pipe without hitting the per-frame limit below.
constexpr size_t kChunkSize = 1u << 20;  // 1 MiB per chunk frame
constexpr size_t kDirectMax = 16u << 20; // ≤16 MiB goes in a single frame

/// Write one message to fd.  Large payloads (> kDirectMax) are transparently
/// split into chunk frames.  Returns false on write error / truncation.
bool write_frame(int fd, MsgType type, const std::vector<uint8_t>& payload);

/// Read one message from fd (blocking).  Chunked sequences are transparently
/// reassembled into a single payload.  Returns false on EOF or protocol error.
bool read_frame(int fd, MsgType& out_type, std::vector<uint8_t>& out_payload);

// ── AlgorithmOutput codec ───────────────────────────────────────────────

/// Encode an AlgorithmOutput (the worker's final result) as a frame payload.
/// `solutions` (vector<EnchSolution>, non-trivially-copyable) is written via
/// each element's serialize(); strings/Item/int64 go through ByteStream.
std::vector<uint8_t> encode_algorithm_output(const AlgorithmOutput& out);

/// Decode the above.  Returns false on malformed input.
bool decode_algorithm_output(const std::vector<uint8_t>& bytes, AlgorithmOutput& out);

// ── Payload helpers (ByteStream codecs) ────────────────────────────

/// Encode a trivially-serializable value into a frame payload.
template <typename T> std::vector<uint8_t> encode_value(const T& v) {
    ByteStreamWriter w;
    w << v;
    return std::move(w).take();
}

/// Encode any IBinarySerializable object into a frame payload.
template <typename T> std::vector<uint8_t> encode(const T& obj) {
    ByteStreamWriter w;
    obj.serialize(w);
    return std::move(w).take();
}

/// Decode a frame payload into an IBinarySerializable object.
template <typename T> bool decode(const std::vector<uint8_t>& payload, T& obj) {
    ByteStreamReader r(payload);
    obj.deserialize(r);
    return r.ok();
}

} // namespace algorithm::ipc
