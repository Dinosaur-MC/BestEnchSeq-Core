#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// ─── ByteStreamWriter: append-only binary writer (little-endian) ──────────
class ByteStreamWriter {
public:
    ByteStreamWriter() { _buf.reserve(4096); }

    void u8(uint8_t v)  { _buf.push_back(v); }
    void u16(uint16_t v) {
        _buf.push_back(static_cast<uint8_t>(v));
        _buf.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        _buf.push_back(static_cast<uint8_t>(v));
        _buf.push_back(static_cast<uint8_t>(v >> 8));
        _buf.push_back(static_cast<uint8_t>(v >> 16));
        _buf.push_back(static_cast<uint8_t>(v >> 24));
    }
    void u64(uint64_t v) {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }

    void i8(int8_t v)  { u8(static_cast<uint8_t>(v)); }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }

    void bytes(const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        _buf.insert(_buf.end(), p, p + len);
    }

    /// Length-prefixed string: u32(size) + raw bytes
    void string(std::string_view s) {
        u32(static_cast<uint32_t>(s.size()));
        bytes(s.data(), s.size());
    }

    const std::vector<uint8_t>& data() const& { return _buf; }
    std::vector<uint8_t> take() && { return std::move(_buf); }
    void clear() { _buf.clear(); }

private:
    std::vector<uint8_t> _buf;
};

// ─── ByteStreamReader: bounded binary reader (little-endian) ─────────────
class ByteStreamReader {
public:
    explicit ByteStreamReader(const std::vector<uint8_t>& data)
        : _pos(data.data()), _end(data.data() + data.size()), _ok(true) {}

    ByteStreamReader(const uint8_t* data, size_t size)
        : _pos(data), _end(data + size), _ok(true) {}

    bool ok()      const { return _ok; }
    bool fail()     const noexcept { return !_ok; }
    void set_fail()       noexcept { _ok = false; }
    bool has_more() const { return _ok && _pos < _end; }
    const uint8_t* pos() const { return _pos; }
    size_t remaining() const { return _ok && _pos <= _end
                                   ? static_cast<size_t>(_end - _pos) : 0; }

    uint8_t u8() {
        if (!_ok || _pos + 1 > _end) { _ok = false; return 0; }
        return *_pos++;
    }
    uint16_t u16() {
        if (!_ok || _pos + 2 > _end) { _ok = false; return 0; }
        uint16_t v = static_cast<uint16_t>(_pos[0])
                   | static_cast<uint16_t>(_pos[1]) << 8;
        _pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!_ok || _pos + 4 > _end) { _ok = false; return 0; }
        uint32_t v = static_cast<uint32_t>(_pos[0])
                   | static_cast<uint32_t>(_pos[1]) << 8
                   | static_cast<uint32_t>(_pos[2]) << 16
                   | static_cast<uint32_t>(_pos[3]) << 24;
        _pos += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32();
        uint64_t hi = u32();
        return lo | (hi << 32);
    }

    int8_t   i8()  { return static_cast<int8_t>(u8()); }
    int16_t  i16() { return static_cast<int16_t>(u16()); }
    int32_t  i32() { return static_cast<int32_t>(u32()); }
    int64_t  i64() { return static_cast<int64_t>(u64()); }

    std::string string() {
        uint32_t len = u32();
        if (!_ok || _pos + len > _end) { _ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(_pos), len);
        _pos += len;
        return s;
    }

    void skip(size_t n) {
        if (!_ok || _pos + n > _end) { _ok = false; return; }
        _pos += n;
    }

    /// Read n bytes as a new vector.  Returns empty on failure (sets _ok=false).
    std::vector<uint8_t> read_bytes(size_t n) {
        if (!_ok || _pos + n > _end) { _ok = false; return {}; }
        std::vector<uint8_t> result(_pos, _pos + n);
        _pos += n;
        return result;
    }

private:
    const uint8_t* _pos;
    const uint8_t* _end;
    bool _ok;
};
