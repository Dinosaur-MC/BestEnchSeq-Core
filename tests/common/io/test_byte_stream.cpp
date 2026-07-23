#include "common/io/ByteStream.h"
#include "framework/test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ===========================================================================
// ByteStreamWriter — 常规写入语义
// ===========================================================================

void test_write_u8() {
    ByteStreamWriter w;
    w.write(uint8_t(0xAB));
    auto data = std::move(w).take();
    expect(data.size() == 1, "write(u8): size = 1");
    expect(data[0] == 0xAB, "write(u8): value = 0xAB");
    std::cout << "  PASS: test_write_u8" << std::endl;
}

void test_write_u16() {
    ByteStreamWriter w;
    w.write(uint16_t(0x1234));
    auto data = std::move(w).take();
    expect(data.size() == 2, "write(u16): size = 2");
    expect(data[0] == 0x34, "write(u16): LE low byte");
    expect(data[1] == 0x12, "write(u16): LE high byte");
    std::cout << "  PASS: test_write_u16" << std::endl;
}

void test_write_u32() {
    ByteStreamWriter w;
    w.write(uint32_t(0xDEADBEEF));
    auto data = std::move(w).take();
    expect(data.size() == 4, "write(u32): size = 4");
    expect(data[0] == 0xEF, "write(u32): LE byte 0");
    expect(data[3] == 0xDE, "write(u32): LE byte 3");
    std::cout << "  PASS: test_write_u32" << std::endl;
}

void test_write_u64() {
    ByteStreamWriter w;
    w.write(uint64_t(0x0102030405060708ULL));
    auto data = std::move(w).take();
    expect(data.size() == 8, "write(u64): size = 8");
    expect(data[0] == 0x08, "write(u64): LE byte 0");
    expect(data[7] == 0x01, "write(u64): LE byte 7");
    std::cout << "  PASS: test_write_u64" << std::endl;
}

void test_write_bool() {
    ByteStreamWriter w;
    w.write(true);
    w.write(false);
    auto data = std::move(w).take();
    expect(data.size() == 2, "write(bool): size = 2");
    expect(data[0] == 1, "write(true): byte = 1");
    expect(data[1] == 0, "write(false): byte = 0");
    std::cout << "  PASS: test_write_bool" << std::endl;
}

void test_write_bytes() {
    ByteStreamWriter w;
    uint8_t src[] = {10, 20, 30};
    w.bytes(src, 3);
    auto data = std::move(w).take();
    expect(data.size() == 3, "write(bytes): size = 3");
    expect(data[0] == 10 && data[1] == 20 && data[2] == 30, "write(bytes): values");
    std::cout << "  PASS: test_write_bytes" << std::endl;
}

void test_write_string() {
    ByteStreamWriter w;
    w.string("hello");
    auto data = std::move(w).take();
    expect(data.size() == 5 + sizeof(size_t), "string: size = 5 + sizeof(size_t)");
    // Check length prefix
    size_t len = 0;
    std::memcpy(&len, data.data(), sizeof(size_t));
    expect(len == 5, "string: length prefix = 5");
    expect(data[sizeof(size_t)] == 'h', "string: first char = 'h'");
    std::cout << "  PASS: test_write_string" << std::endl;
}

void test_operator_shift_writer() {
    ByteStreamWriter w;
    w << uint32_t(0x12345678) << uint16_t(0xAABB) << uint8_t(0xFF) << true;
    auto data = std::move(w).take();
    expect(data.size() == 8, "operator<< writer: size = 4+2+1+1 = 8");
    expect(data[0] == 0x78, "operator<< writer: u32 LE byte 0");
    expect(data[4] == 0xBB, "operator<< writer: u16 LE byte 0");
    expect(data[6] == 0xFF, "operator<< writer: u8");
    expect(data[7] == 1,    "operator<< writer: bool true = 1");
    std::cout << "  PASS: test_operator_shift_writer" << std::endl;
}

void test_write_vector() {
    std::vector<uint32_t> vec = {0x11111111, 0x22222222, 0x33333333};
    ByteStreamWriter w;
    w << vec;
    auto data = std::move(w).take();
    // size prefix (sizeof(size_t)) + 3 * 4 bytes
    size_t expected_size = sizeof(size_t) + 12;
    expect(data.size() == expected_size, "write vector: correct total size");

    size_t n = 0;
    std::memcpy(&n, data.data(), sizeof(size_t));
    expect(n == 3, "write vector: count = 3");
    expect(data[sizeof(size_t)] == 0x11, "write vector: first element LE byte 0");
    std::cout << "  PASS: test_write_vector" << std::endl;
}

void test_write_blob() {
    std::vector<uint8_t> blob = {10, 20, 30, 40};
    ByteStreamWriter w;
    w << blob;
    auto data = std::move(w).take();
    expect(data.size() == sizeof(size_t) + 4, "write blob: correct size");
    size_t n = 0;
    std::memcpy(&n, data.data(), sizeof(size_t));
    expect(n == 4, "write blob: count = 4");
    expect(data[sizeof(size_t) + 2] == 30, "write blob: value check");
    std::cout << "  PASS: test_write_blob" << std::endl;
}

void test_writer_clear() {
    ByteStreamWriter w;
    w << uint32_t(0xDEADBEEF);
    expect(w.data().size() == 4, "writer clear: has data before clear");
    w.clear();
    expect(w.data().empty(), "writer clear: empty after clear");
    std::cout << "  PASS: test_writer_clear" << std::endl;
}

void test_writer_take() {
    ByteStreamWriter w;
    w << uint32_t(42);
    auto d1 = std::move(w).take();
    expect(d1.size() == 4, "writer take: has data");
    expect(w.data().empty(), "writer take: original empty after move");
    std::cout << "  PASS: test_writer_take" << std::endl;
}

// ===========================================================================
// ByteStreamReader — 常规读取语义
// ===========================================================================

void test_read_u8() {
    uint8_t buf[] = {0xAB, 0xCD};
    ByteStreamReader r(buf, sizeof(buf));
    expect(r.u8() == 0xAB, "read u8: first byte");
    expect(r.u8() == 0xCD, "read u8: second byte");
    expect(r.ok(), "read u8: ok after valid reads");
    std::cout << "  PASS: test_read_u8" << std::endl;
}

void test_read_u16() {
    uint8_t buf[] = {0x34, 0x12, 0x78, 0x56};
    ByteStreamReader r(buf, sizeof(buf));
    expect(r.u16() == 0x1234, "read u16: LE 0x1234");
    expect(r.u16() == 0x5678, "read u16: LE 0x5678");
    expect(r.ok(), "read u16: ok after valid reads");
    std::cout << "  PASS: test_read_u16" << std::endl;
}

void test_read_u32() {
    uint8_t buf[] = {0xEF, 0xBE, 0xAD, 0xDE, 0x01, 0x00, 0x00, 0x00};
    ByteStreamReader r(buf, sizeof(buf));
    expect(r.u32() == 0xDEADBEEF, "read u32: LE 0xDEADBEEF");
    expect(r.u32() == 1, "read u32: LE 1");
    expect(r.ok(), "read u32: ok after valid reads");
    std::cout << "  PASS: test_read_u32" << std::endl;
}

void test_read_u64() {
    uint8_t buf[] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    ByteStreamReader r(buf, sizeof(buf));
    expect(r.u64() == 0x0102030405060708ULL, "read u64: LE value");
    expect(r.ok(), "read u64: ok after valid read");
    std::cout << "  PASS: test_read_u64" << std::endl;
}

void test_read_bool() {
    uint8_t buf[] = {1, 0, 0xFF};
    ByteStreamReader r(buf, sizeof(buf));
    bool v1, v2, v3;
    r >> v1 >> v2 >> v3;
    expect(v1 == true,  "read bool: 1 → true");
    expect(v2 == false, "read bool: 0 → false");
    expect(v3 == true,  "read bool: 0xFF → true (non-zero)");
    expect(r.ok(), "read bool: ok after valid reads");
    std::cout << "  PASS: test_read_bool" << std::endl;
}

void test_read_string() {
    uint8_t buf[] = {
        5, 0, 0, 0, 0, 0, 0, 0,  // size_t length prefix = 5
        'h', 'e', 'l', 'l', 'o'
    };
    ByteStreamReader r(buf, sizeof(buf));
    auto s = r.string();
    expect(s == "hello", "read string: content = 'hello'");
    expect(r.ok(), "read string: ok after valid read");
    std::cout << "  PASS: test_read_string" << std::endl;
}

void test_operator_shift_reader() {
    uint8_t buf[] = {
        0x78, 0x56, 0x34, 0x12,  // u32 = 0x12345678
        0xBB, 0xAA,              // u16 = 0xAABB
        0xFF,                     // u8  = 0xFF
        1                         // bool = true
    };
    ByteStreamReader r(buf, sizeof(buf));
    uint32_t a;
    uint16_t b;
    uint8_t c;
    bool d;
    r >> a >> b >> c >> d;
    expect(a == 0x12345678, "operator>>: u32");
    expect(b == 0xAABB,     "operator>>: u16");
    expect(c == 0xFF,       "operator>>: u8");
    expect(d == true,       "operator>>: bool");
    expect(r.ok(), "operator>>: ok after all reads");
    std::cout << "  PASS: test_operator_shift_reader" << std::endl;
}

void test_read_vector() {
    uint8_t buf[] = {
        3, 0, 0, 0, 0, 0, 0, 0,  // count = 3
        0x11, 0x11, 0x11, 0x11,  // elem[0] = 0x11111111
        0x22, 0x22, 0x22, 0x22,  // elem[1] = 0x22222222
        0x33, 0x33, 0x33, 0x33,  // elem[2] = 0x33333333
    };
    ByteStreamReader r(buf, sizeof(buf));
    std::vector<uint32_t> vec;
    r >> vec;
    expect(r.ok(),          "read vector: ok");
    expect(vec.size() == 3, "read vector: size = 3");
    expect(vec[0] == 0x11111111, "read vector: elem[0]");
    expect(vec[1] == 0x22222222, "read vector: elem[1]");
    expect(vec[2] == 0x33333333, "read vector: elem[2]");
    std::cout << "  PASS: test_read_vector" << std::endl;
}

void test_read_blob() {
    uint8_t buf[] = {
        4, 0, 0, 0, 0, 0, 0, 0,  // count = 4
        10, 20, 30, 40
    };
    ByteStreamReader r(buf, sizeof(buf));
    std::vector<uint8_t> blob;
    r >> blob;
    expect(r.ok(),          "read blob: ok");
    expect(blob.size() == 4, "read blob: size = 4");
    expect(blob[0] == 10 && blob[3] == 40, "read blob: values");
    std::cout << "  PASS: test_read_blob" << std::endl;
}

void test_read_bytes() {
    uint8_t buf[] = {10, 20, 30, 40, 50};
    ByteStreamReader r(buf, sizeof(buf));
    auto chunk = r.read_bytes(3);
    expect(chunk.size() == 3, "read_bytes: size = 3");
    expect(chunk[0] == 10 && chunk[2] == 30, "read_bytes: values");
    expect(r.remaining() == 2, "read_bytes: remaining = 2");
    expect(r.ok(), "read_bytes: ok");
    std::cout << "  PASS: test_read_bytes" << std::endl;
}

void test_skip() {
    uint8_t buf[] = {1, 2, 3, 4, 5};
    ByteStreamReader r(buf, sizeof(buf));
    r.skip(3);
    expect(r.ok(), "skip: ok within bounds");
    expect(r.u8() == 4, "skip: next byte after skip");
    std::cout << "  PASS: test_skip" << std::endl;
}

void test_has_more() {
    uint8_t buf[] = {1, 2};
    ByteStreamReader r(buf, sizeof(buf));
    expect(r.has_more() == true, "has_more: before reads");
    r.u8(); r.u8();
    expect(r.has_more() == false, "has_more: after consuming all");
    std::cout << "  PASS: test_has_more" << std::endl;
}

void test_pos() {
    uint8_t buf[] = {10, 20, 30};
    ByteStreamReader r(buf, sizeof(buf));
    expect(r.pos() == buf, "pos: starts at beginning");
    r.u8();
    expect(r.pos() == buf + 1, "pos: advances after read");
    std::cout << "  PASS: test_pos" << std::endl;
}

// ===========================================================================
// ByteStreamWriter → ByteStreamReader Round-trip
// ===========================================================================

void test_roundtrip_basic() {
    ByteStreamWriter w;
    w << uint8_t(0x12) << uint16_t(0x3456) << uint32_t(0x789ABCDE)
      << uint64_t(0xFEDCBA9876543210ULL) << true << false;

    auto data = w.data();
    ByteStreamReader r(data);

    uint8_t  a; uint16_t b; uint32_t c; uint64_t d; bool e; bool f;
    r >> a >> b >> c >> d >> e >> f;

    expect(a == 0x12,                   "roundtrip: u8");
    expect(b == 0x3456,                 "roundtrip: u16");
    expect(c == 0x789ABCDE,             "roundtrip: u32");
    expect(d == 0xFEDCBA9876543210ULL,  "roundtrip: u64");
    expect(e == true,                   "roundtrip: bool true");
    expect(f == false,                  "roundtrip: bool false");
    expect(r.ok(),                      "roundtrip: ok");
    expect(!r.has_more(),               "roundtrip: fully consumed");
    std::cout << "  PASS: test_roundtrip_basic" << std::endl;
}

void test_roundtrip_string() {
    ByteStreamWriter w;
    w << std::string_view("你好世界");

    ByteStreamReader r(w.data());
    std::string s;
    r >> s;
    expect(s == "你好世界", "roundtrip string: UTF-8 content");
    expect(r.ok(), "roundtrip string: ok");
    std::cout << "  PASS: test_roundtrip_string" << std::endl;
}

void test_roundtrip_vector() {
    std::vector<int32_t> src = {-100, 0, 42, 999999};

    ByteStreamWriter w;
    w << src;

    ByteStreamReader r(w.data());
    std::vector<int32_t> dst;
    r >> dst;

    expect(dst.size() == src.size(), "roundtrip vector: same size");
    for (size_t i = 0; i < src.size(); ++i)
        expect(dst[i] == src[i], "roundtrip vector: element match");
    expect(r.ok(), "roundtrip vector: ok");
    std::cout << "  PASS: test_roundtrip_vector" << std::endl;
}

void test_roundtrip_blob() {
    std::vector<uint8_t> src = {0, 1, 2, 255, 128, 64};

    ByteStreamWriter w;
    w << src;

    ByteStreamReader r(w.data());
    std::vector<uint8_t> dst;
    r >> dst;

    expect(dst.size() == src.size(), "roundtrip blob: same size");
    for (size_t i = 0; i < src.size(); ++i)
        expect(dst[i] == src[i], "roundtrip blob: element match");
    expect(r.ok(), "roundtrip blob: ok");
    std::cout << "  PASS: test_roundtrip_blob" << std::endl;
}

void test_roundtrip_mixed() {
    ByteStreamWriter w;
    w << uint8_t(1) << std::string_view("mix")
      << uint32_t(100) << true
      << std::vector<uint8_t>{7, 8, 9};

    ByteStreamReader r(w.data());
    uint8_t a; std::string b; uint32_t c; bool d; std::vector<uint8_t> e;
    r >> a >> b >> c >> d >> e;

    expect(a == 1,           "roundtrip mixed: u8");
    expect(b == "mix",       "roundtrip mixed: string");
    expect(c == 100,         "roundtrip mixed: u32");
    expect(d == true,        "roundtrip mixed: bool");
    expect(e.size() == 3,    "roundtrip mixed: blob size");
    expect(e[2] == 9,        "roundtrip mixed: blob[2]");
    expect(r.ok(),           "roundtrip mixed: ok");
    expect(!r.has_more(),    "roundtrip mixed: fully consumed");
    std::cout << "  PASS: test_roundtrip_mixed" << std::endl;
}

// ===========================================================================
// 边界与错误处理
// ===========================================================================

void test_read_beyond_eof() {
    uint8_t buf[] = {1, 2};
    ByteStreamReader r(buf, sizeof(buf));
    r.u32();  // need 4 bytes, only 2 available
    expect(r.fail() == true, "read beyond EOF: fail set");
    expect(r.ok() == false,  "read beyond EOF: ok false");
    std::cout << "  PASS: test_read_beyond_eof" << std::endl;
}

void test_read_bytes_beyond_eof() {
    uint8_t buf[] = {10, 20};
    ByteStreamReader r(buf, sizeof(buf));
    auto chunk = r.read_bytes(10);
    expect(chunk.empty(), "read_bytes beyond EOF: returns empty");
    expect(r.fail(), "read_bytes beyond EOF: fail set");
    std::cout << "  PASS: test_read_bytes_beyond_eof" << std::endl;
}

void test_skip_beyond_eof() {
    uint8_t buf[] = {1};
    ByteStreamReader r(buf, sizeof(buf));
    r.skip(100);
    expect(r.fail(), "skip beyond EOF: fail set");
    std::cout << "  PASS: test_skip_beyond_eof" << std::endl;
}

void test_string_beyond_eof() {
    // Length says 100 bytes, but buffer is shorter
    uint8_t buf[] = {
        100, 0, 0, 0, 0, 0, 0, 0,  // size_t length prefix = 100
        'a', 'b', 'c'
    };
    ByteStreamReader r(buf, sizeof(buf));
    auto s = r.string();
    expect(s.empty(), "string beyond EOF: returns empty");
    expect(r.fail(), "string beyond EOF: fail set");
    std::cout << "  PASS: test_string_beyond_eof" << std::endl;
}

void test_empty_buffer() {
    ByteStreamReader r(nullptr, 0);
    expect(r.has_more() == false, "empty buffer: has_more false");
    expect(r.remaining() == 0,    "empty buffer: remaining 0");
    expect(r.u8() == 0,           "empty buffer: u8 returns 0");
    expect(r.fail(),              "empty buffer: fail set");
    std::cout << "  PASS: test_empty_buffer" << std::endl;
}

void test_take_empties_writer() {
    ByteStreamWriter w;
    w << uint32_t(0x12345678);
    auto d1 = std::move(w).take();
    expect(d1.size() == 4, "take: returned data has 4 bytes");
    // w is moved-from — data() is empty
    expect(w.data().empty(), "take: original writer empty");
    std::cout << "  PASS: test_take_empties_writer" << std::endl;
}

void test_i8_i16_i32_i64() {
    ByteStreamWriter w;
    w << int8_t(-1) << int16_t(-128) << int32_t(-100000) << int64_t(-1LL << 40);

    ByteStreamReader r(w.data());
    int8_t  a; int16_t b; int32_t c; int64_t d;
    r >> a >> b >> c >> d;

    expect(a == -1,            "i8: -1");
    expect(b == -128,          "i16: -128");
    expect(c == -100000,       "i32: -100000");
    expect(d == (-1LL << 40),  "i64: -2^40");
    expect(r.ok(), "signed ints: ok");
    std::cout << "  PASS: test_i8_i16_i32_i64" << std::endl;
}

void test_multiple_writes_accumulate() {
    ByteStreamWriter w;
    w << uint8_t(1);
    w << uint8_t(2);
    w << uint8_t(3);
    expect(w.data().size() == 3, "accumulate: 3 bytes");
    expect(w.data()[0] == 1 && w.data()[2] == 3, "accumulate: values");
    std::cout << "  PASS: test_multiple_writes_accumulate" << std::endl;
}

void test_signed_convenience_wrappers() {
    ByteStreamWriter w;
    w.i8(-1);
    w.i16(-20000);
    w.i32(-2000000000);
    w.i64(-9000000000000000000LL);

    ByteStreamReader r(w.data());
    expect(r.i8()  == -1,                  "i8 wrapper");
    expect(r.i16() == -20000,              "i16 wrapper");
    expect(r.i32() == -2000000000,         "i32 wrapper");
    expect(r.i64() == -9000000000000000000LL, "i64 wrapper");
    expect(r.ok(), "signed wrappers: ok");
    std::cout << "  PASS: test_signed_convenience_wrappers" << std::endl;
}

void test_u8_convenience_wrappers_writer() {
    ByteStreamWriter w;
    w.u8(0x12);
    w.u16(0x3456);
    w.u32(0x789ABCDE);
    w.u64(0xFEDCBA9876543210ULL);

    ByteStreamReader r(w.data());
    expect(r.u8()  == 0x12,                   "u8 wrapper");
    expect(r.u16() == 0x3456,                 "u16 wrapper");
    expect(r.u32() == 0x789ABCDE,             "u32 wrapper");
    expect(r.u64() == 0xFEDCBA9876543210ULL,  "u64 wrapper");
    expect(r.ok(), "unsigned wrappers: ok");
    std::cout << "  PASS: test_u8_convenience_wrappers_writer" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== ByteStream Writer Tests ===" << std::endl;
    test_write_u8();
    test_write_u16();
    test_write_u32();
    test_write_u64();
    test_write_bool();
    test_write_bytes();
    test_write_string();
    test_operator_shift_writer();
    test_write_vector();
    test_write_blob();
    test_writer_clear();
    test_writer_take();

    std::cout << "\n=== ByteStream Reader Tests ===" << std::endl;
    test_read_u8();
    test_read_u16();
    test_read_u32();
    test_read_u64();
    test_read_bool();
    test_read_string();
    test_operator_shift_reader();
    test_read_vector();
    test_read_blob();
    test_read_bytes();
    test_skip();
    test_has_more();
    test_pos();

    std::cout << "\n=== ByteStream Round-trip Tests ===" << std::endl;
    test_roundtrip_basic();
    test_roundtrip_string();
    test_roundtrip_vector();
    test_roundtrip_blob();
    test_roundtrip_mixed();

    std::cout << "\n=== ByteStream Edge Cases ===" << std::endl;
    test_read_beyond_eof();
    test_read_bytes_beyond_eof();
    test_skip_beyond_eof();
    test_string_beyond_eof();
    test_empty_buffer();
    test_take_empties_writer();
    test_i8_i16_i32_i64();
    test_multiple_writes_accumulate();
    test_signed_convenience_wrappers();
    test_u8_convenience_wrappers_writer();

    return print_summary();
}
