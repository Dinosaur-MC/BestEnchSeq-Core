#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

template <typename T>
concept TrivialSerializable = std::is_trivially_copyable_v<T> && !std::is_same_v<T, bool>;

// ─── ByteStreamWriter: append-only binary writer (little-endian) ──────────

class ByteStreamWriter {
public:
    ByteStreamWriter() { _buf.reserve(4096); }

    /// Write any trivially copyable value in little-endian byte order.
    template <TrivialSerializable T>
    void write(T v) noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            auto* p = reinterpret_cast<uint8_t*>(&v);
            for (size_t i = 0; i < sizeof(T) / 2; ++i)
                std::swap(p[i], p[sizeof(T) - 1 - i]);
        }
        auto* p = reinterpret_cast<const uint8_t*>(&v);
        _buf.insert(_buf.end(), p, p + sizeof(T));
    }

    void bytes(const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        _buf.insert(_buf.end(), p, p + len);
    }

    /// Length-prefixed string: size + raw bytes.
    void string(std::string_view s) {
        write(s.size());
        bytes(s.data(), s.size());
    }

    // ── operator<< ──

    template <TrivialSerializable T>
    ByteStreamWriter& operator<<(T v) noexcept { write(v); return *this; }

    ByteStreamWriter& operator<<(bool v) noexcept { write(v); return *this; }

    /// Catch string literals — without this they'd match the template and
    /// serialize the pointer value instead of the characters.
    ByteStreamWriter& operator<<(const char* s) noexcept { string(s); return *this; }

    // ── Special case: bool serializes as 1 byte (uint8_t) ──
    void write(bool v) { _buf.push_back(static_cast<uint8_t>(v)); }

    // ── Write convenience — thin wrappers for call-site compatibility ──
    void u8(uint8_t   v) noexcept { write(v); }
    void u16(uint16_t v) noexcept { write(v); }
    void u32(uint32_t v) noexcept { write(v); }
    void u64(uint64_t v) noexcept { write(v); }
    void i8(int8_t    v) noexcept { write(v); }
    void i16(int16_t  v) noexcept { write(v); }
    void i32(int32_t  v) noexcept { write(v); }
    void i64(int64_t  v) noexcept { write(v); }

    ByteStreamWriter& operator<<(std::string_view s) noexcept { string(s); return *this; }

    template <TrivialSerializable T>
    ByteStreamWriter& operator<<(const std::vector<T>& vec) noexcept {
        *this << vec.size();
        for (const auto& v : vec) *this << v;
        return *this;
    }

    ByteStreamWriter& operator<<(const std::vector<uint8_t>& blob) noexcept {
        *this << blob.size();
        bytes(blob.data(), blob.size());
        return *this;
    }

    const std::vector<uint8_t>& data() const& noexcept { return _buf; }
    std::vector<uint8_t> take() && noexcept { return std::move(_buf); }
    void clear() noexcept { _buf.clear(); }

private:
    std::vector<uint8_t> _buf;
};

// ─── ByteStreamReader: bounded binary reader (little-endian) ─────────────

class ByteStreamReader {
public:
    explicit ByteStreamReader(const std::vector<uint8_t>& data) noexcept
        : _pos(data.data()), _end(data.data() + data.size()), _ok(true) {}

    ByteStreamReader(const uint8_t* data, size_t size) noexcept
        : _pos(data), _end(data + size), _ok(true) {}

    bool ok()       const noexcept { return _ok; }
    bool fail()     const noexcept { return !_ok; }
    void set_fail()       noexcept { _ok = false; }
    bool has_more() const noexcept { return _ok && _pos < _end; }
    const uint8_t* pos() const noexcept { return _pos; }

    size_t remaining() const noexcept {
        return _ok && _pos <= _end
                   ? static_cast<size_t>(_end - _pos) : 0;
    }

    /// Read any trivially copyable value in little-endian byte order.
    template <TrivialSerializable T>
    void read(T& v) noexcept {
        if (!_ok || _pos + sizeof(T) > _end) { _ok = false; return; }
        std::memcpy(&v, _pos, sizeof(T));
        _pos += sizeof(T);
        if constexpr (std::endian::native == std::endian::big) {
            auto* p = reinterpret_cast<uint8_t*>(&v);
            for (size_t i = 0; i < sizeof(T) / 2; ++i)
                std::swap(p[i], p[sizeof(T) - 1 - i]);
        }
    }

    std::string string() {
        size_t len;
        read(len);
        if (!_ok || len > remaining()) { _ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(_pos), len);
        _pos += len;
        return s;
    }

    void skip(size_t n) noexcept {
        if (!_ok || n > remaining()) { _ok = false; return; }
        _pos += n;
    }

    /// Read n bytes as a new vector.  Returns empty on failure (sets _ok=false).
    std::vector<uint8_t> read_bytes(size_t n) {
        if (!_ok || n > remaining()) { _ok = false; return {}; }
        std::vector<uint8_t> result(_pos, _pos + n);
        _pos += n;
        return result;
    }

    // ── operator>> ──

    template <TrivialSerializable T>
    ByteStreamReader& operator>>(T& v) noexcept { read(v); return *this; }

    ByteStreamReader& operator>>(bool& v) noexcept { read(v); return *this; }

    // ── Special case: bool reads as 1 byte (uint8_t) ──
    void read(bool& v) noexcept {
        if (!_ok || _pos + 1 > _end) { _ok = false; return; }
        v = (*_pos++) != 0;
    }

    // ── Read convenience — thin wrappers for expression-context usage ──
    uint8_t  u8()  noexcept { uint8_t  v{}; read(v); return v; }
    uint16_t u16() noexcept { uint16_t v{}; read(v); return v; }
    uint32_t u32() noexcept { uint32_t v{}; read(v); return v; }
    uint64_t u64() noexcept { uint64_t v{}; read(v); return v; }
    int8_t   i8()  noexcept { int8_t   v{}; read(v); return v; }
    int16_t  i16() noexcept { int16_t  v{}; read(v); return v; }
    int32_t  i32() noexcept { int32_t  v{}; read(v); return v; }
    int64_t  i64() noexcept { int64_t  v{}; read(v); return v; }

    ByteStreamReader& operator>>(std::string& s) { s = string(); return *this; }

    template <TrivialSerializable T>
    ByteStreamReader& operator>>(std::vector<T>& vec) noexcept {
        size_t n{};
        read(n);
        if (!_ok) { vec.clear(); return *this; }
        vec.resize(n);
        for (auto& v : vec) *this >> v;
        return *this;
    }

    ByteStreamReader& operator>>(std::vector<uint8_t>& blob) noexcept {
        size_t n{};
        read(n);
        if (!_ok) { blob.clear(); return *this; }
        blob = read_bytes(n);
        return *this;
    }

private:
    const uint8_t* _pos;
    const uint8_t* _end;
    bool _ok;
};
