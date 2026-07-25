#pragma once

/// Generic serialization base — no format-specific methods.
/// Format-specific interfaces inherit from this:
///   IJsonSerializable   — JSON (to_json / from_json)
///   IBinarySerializable — Binary ByteStream (serialize / deserialize)
struct ISerializable {
    virtual ~ISerializable() = default;
};
