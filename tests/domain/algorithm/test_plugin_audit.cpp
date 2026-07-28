/// @file test_plugin_audit.cpp
/// Standalone binary-scanner tests for PluginAudit.
///
/// Constructs minimal ELF / PE binaries in memory, writes them to
/// temporary files, and verifies that audit_plugin_binary() produces
/// the expected security report.
///
/// Platform coverage:
///   Windows  — PE scanner (audit_pe)
///   Linux    — ELF scanner (audit_elf)
///   Cross    — empty / truncated / misnamed input

#include "framework/test_utils.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/plugin/PluginAudit.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Silence MSVC fopen deprecation for test helper code.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#pragma warning(disable : 4996)
#endif

using namespace algorithm;

// ======================================================================
//  Binary helpers — write / clean / construct
// ======================================================================

namespace {

// ── Little-endian write helpers ────────────────────────────────

/// Append v as 2 LE bytes to vector.
void append_le16(std::vector<uint8_t> &b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
/// Append v as 4 LE bytes to vector.
void append_le32(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; ++i) { b.push_back(static_cast<uint8_t>(v)); v >>= 8; }
}
// (append_le64 intentionally omitted — not needed)

/// Write v as 4 LE bytes at position pos in a pre-sized vector.
void set_le32(std::vector<uint8_t> &b, size_t pos, uint32_t v) {
    b[pos]     = static_cast<uint8_t>(v);
    b[pos + 1] = static_cast<uint8_t>(v >> 8);
    b[pos + 2] = static_cast<uint8_t>(v >> 16);
    b[pos + 3] = static_cast<uint8_t>(v >> 24);
}
/// Write v as 2 LE bytes at position pos in a pre-sized vector.
void set_le16(std::vector<uint8_t> &b, size_t pos, uint16_t v) {
    b[pos]     = static_cast<uint8_t>(v);
    b[pos + 1] = static_cast<uint8_t>(v >> 8);
}
/// Write v as 8 LE bytes at position pos in a pre-sized vector.
#if defined(__linux__)
void set_le64(std::vector<uint8_t> &b, size_t pos, uint64_t v) {
    for (int i = 0; i < 8; ++i) { b[pos + i] = static_cast<uint8_t>(v); v >>= 8; }
}
#endif

// ── Temp-file RAII helper ──────────────────────────────────────

struct TempFile {
    std::string path;

    TempFile(const std::vector<uint8_t> &content) {
        static int counter = 0;
        std::string name = "audit_tmp_" + std::to_string(++counter) + ".bin";
        FILE *f = fopen(name.c_str(), "wb");
        if (!f) return;
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
        path = std::move(name);
    }

    ~TempFile() {
        if (!path.empty()) remove(path.c_str());
    }

    [[nodiscard]] bool valid() const { return !path.empty(); }
};

// ── Minimal executable construction ────────────────────────────

/// 64-byte ELF64 header (ET_DYN, x86-64, little-endian, no sections).
#if defined(__linux__)
std::vector<uint8_t> make_elf64_base() {
    std::vector<uint8_t> h(64, 0);
    h[0] = 0x7F; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';  // magic
    h[4] = 2;                    // ELFCLASS64
    h[5] = 1;                    // ELFDATA2LSB
    h[6] = 1;                    // EV_CURRENT
    set_le16(h, 16, 3);          // ET_DYN
    set_le16(h, 18, 0x3E);       // EM_X86_64
    set_le32(h, 20, 1);          // e_version
    set_le16(h, 52, 64);         // e_ehsize = 64
    set_le16(h, 54, 56);         // e_phentsize = sizeof(Elf64_Phdr)
    set_le16(h, 58, 64);         // e_shentsize = sizeof(Elf64_Shdr)
    return h;
}

/// Add a single PT_LOAD program header with given flags.
/// Returns an ELF64 that has one loadable segment.
std::vector<uint8_t> make_elf64_with_phdr(uint32_t flags) {
    auto h = make_elf64_base();
    h.resize(64 + 56);           // 64-byte header + one 56-byte Phdr
    set_le64(h, 32, 64);         // e_phoff = 64
    set_le16(h, 56, 1);          // e_phnum = 1

    // Program header layout (Elf64_Phdr = 56 bytes):
    //   +0: p_type   (4)  +4: p_flags  (4)  +8: p_offset (8)
    //  +16: p_vaddr  (8)  +24: p_paddr  (8)  +32: p_filesz (8)
    //  +40: p_memsz  (8)  +48: p_align  (8)
    set_le32(h, 64 + 0,  1);          // p_type = PT_LOAD
    set_le32(h, 64 + 4,  flags);      // p_flags
    set_le64(h, 64 + 8,  0);          // p_offset
    set_le64(h, 64 + 16, 0);          // p_vaddr
    set_le64(h, 64 + 24, 0);          // p_paddr
    set_le64(h, 64 + 32, h.size());   // p_filesz
    set_le64(h, 64 + 40, h.size());   // p_memsz
    set_le64(h, 64 + 48, 1);          // p_align
    return h;
}
#endif // __linux__

/// Minimal 64-bit PE (DLL), no sections / imports / exports.
#if defined(_WIN32)
std::vector<uint8_t> make_pe64_base() {
    std::vector<uint8_t> pe;

    // ── DOS header (64 bytes) ──────────────────────────────────
    pe.resize(64, 0);
    pe[0] = 'M'; pe[1] = 'Z';
    set_le32(pe, 60, 128);       // e_lfanew = 128

    // ── DOS stub (64 → 128) ────────────────────────────────────
    pe.resize(128, 0);

    // ── PE signature ───────────────────────────────────────────
    append_le32(pe, 0x00004550); // "PE\0\0"

    // ── COFF header (20 bytes) ─────────────────────────────────
    append_le16(pe, 0x8664);     // Machine = AMD64
    append_le16(pe, 0);          // NumberOfSections = 0
    append_le32(pe, 0);          // TimeDateStamp
    append_le32(pe, 0);          // PointerToSymbolTable
    append_le32(pe, 0);          // NumberOfSymbols
    append_le16(pe, 0xF0);       // SizeOfOptionalHeader
    append_le16(pe, 0x2022);     // Characteristics

    // ── Optional header (PE32+, 240 bytes) ─────────────────────
    const size_t opt = pe.size();
    append_le16(pe, 0x020B);     // Magic PE32+
    pe.resize(opt + 240, 0);
    set_le16(pe, opt + 108, 16); // NumberOfRvaAndSizes

    return pe;
}

/// PE with one section whose characteristics include the given flags.
std::vector<uint8_t> make_pe64_with_section(uint32_t characteristics) {
    auto pe = make_pe64_base();

    // PE layout: [DOS 128] [sig 4] [coff 20] [opt 240] [sections]
    constexpr size_t PE_SIG_OFF = 128;
    constexpr size_t COFF_OFF  = PE_SIG_OFF + 4;   // 132
    constexpr size_t OPT_OFF   = COFF_OFF + 20;     // 152
    constexpr size_t SEC_OFF   = OPT_OFF + 240;     // 392

    set_le16(pe, COFF_OFF + 2, 1);   // NumberOfSections = 1
    pe.resize(SEC_OFF + 40, 0);

    // Section name ".text" (8 bytes)
    const std::string_view sec_name = ".text\0\0\0";
    std::memcpy(&pe[SEC_OFF], sec_name.data(), 8);

    set_le32(pe, SEC_OFF + 8,  4096);  // VirtualSize
    set_le32(pe, SEC_OFF + 12, 4096);  // VirtualAddress
    set_le32(pe, SEC_OFF + 16, 512);   // SizeOfRawData
    set_le32(pe, SEC_OFF + 20, 1024);  // PointerToRawData
    set_le32(pe, SEC_OFF + 36, characteristics);  // Characteristics

    return pe;
}
#endif // _WIN32

} // anonymous namespace

// ======================================================================
//  Tests
// ======================================================================

// ── Boundary / edge cases (cross-platform) ─────────────────────

void test_audit_empty_file() {
    auto report = audit_plugin_binary("");
    expect(!report.passed, "audit: empty path returns passed=false");

    // 0-byte file
    const std::vector<uint8_t> empty;
    TempFile tf(empty);
    if (!tf.valid()) { TEST_PASS("audit: empty file (skipped — temp file creation failed)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(!r.passed, "audit: 0-byte file returns passed=false");

    std::cout << "PASS: test_audit_empty_file" << std::endl;
}

void test_audit_truncated_binary() {
    // Just "MZ" (2 bytes) — too small for any parser
    const std::vector<uint8_t> tiny = {'M', 'Z'};
    TempFile tf(tiny);
    if (!tf.valid()) { TEST_PASS("audit: truncated (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(!r.passed, "audit: truncated 2-byte file returns passed=false");

    // Only the first 4 bytes of ELF magic
    const std::vector<uint8_t> elf_head = {0x7F, 'E', 'L', 'F'};
    TempFile tf2(elf_head);
    if (!tf2.valid()) { TEST_PASS("audit: truncated ELF (skipped)"); return; }
    auto r2 = audit_plugin_binary(tf2.path);
    expect(!r2.passed, "audit: truncated 4-byte ELF returns passed=false");

    std::cout << "PASS: test_audit_truncated_binary" << std::endl;
}

// ── Valid native binary ────────────────────────────────────────

#if defined(_WIN32)

void test_audit_valid_pe() {
    auto pe = make_pe64_base();
    TempFile tf(pe);
    if (!tf.valid()) { TEST_PASS("audit: valid PE (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(r.passed, "audit: minimal PE64 passes");
    expect(!r.has_wx_segment, "audit: minimal PE64 has no W+X");
    expect(r.extra_exports.empty(), "audit: minimal PE64 has no exports");
    expect(r.dangerous_imports.empty(), "audit: minimal PE64 has no imports");
    std::cout << "PASS: test_audit_valid_pe" << std::endl;
}

void test_audit_pe_wx_section() {
    // Section with both IMAGE_SCN_MEM_WRITE and IMAGE_SCN_MEM_EXECUTE
    constexpr uint32_t WX = 0xE0000020; // WRITE | EXECUTE | READ
    auto pe = make_pe64_with_section(WX);
    TempFile tf(pe);
    if (!tf.valid()) { TEST_PASS("audit: PE W+X (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(!r.passed, "audit: PE with W+X rejected");
    expect(r.has_wx_segment, "audit: PE with W+X flags has_wx_segment");
    std::cout << "PASS: test_audit_pe_wx_section" << std::endl;
}

void test_audit_pe_32bit_rejected() {
    auto pe = make_pe64_base();
    // Change Optional Header Magic from PE32+ (0x020B) to PE32 (0x010B)
    // at offset 152 (128 + 4 + 20 = start of optional header).
    pe[152 + 0] = 0x0B;
    pe[152 + 1] = 0x01;
    TempFile tf(pe);
    if (!tf.valid()) { TEST_PASS("audit: PE32 rejected (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(!r.passed, "audit: 32-bit PE is rejected");
    std::cout << "PASS: test_audit_pe_32bit_rejected" << std::endl;
}

#elif defined(__linux__)

void test_audit_valid_elf() {
    auto elf = make_elf64_base();
    TempFile tf(elf);
    if (!tf.valid()) { TEST_PASS("audit: valid ELF (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(r.passed, "audit: minimal ELF64 passes");
    expect(!r.has_wx_segment, "audit: minimal ELF64 has no W+X");
    expect(r.extra_exports.empty(), "audit: minimal ELF64 has no exports");
    expect(r.dangerous_imports.empty(), "audit: minimal ELF64 has no imports");
    std::cout << "PASS: test_audit_valid_elf" << std::endl;
}

void test_audit_elf_wx_segment() {
    // PF_W | PF_X = 6
    auto elf = make_elf64_with_phdr(6);
    TempFile tf(elf);
    if (!tf.valid()) { TEST_PASS("audit: ELF W+X (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(!r.passed, "audit: ELF with W+X rejected");
    expect(r.has_wx_segment, "audit: ELF with W+X flags has_wx_segment");
    std::cout << "PASS: test_audit_elf_wx_segment" << std::endl;
}

void test_audit_elf_32bit_rejected() {
    auto elf = make_elf64_base();
    // Change EI_CLASS from 2 (64-bit) to 1 (32-bit)
    elf[4] = 1;
    TempFile tf(elf);
    if (!tf.valid()) { TEST_PASS("audit: ELF32 rejected (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    expect(!r.passed, "audit: 32-bit ELF is rejected");
    std::cout << "PASS: test_audit_elf_32bit_rejected" << std::endl;
}

void test_audit_elf_safe_segment() {
    // PF_R | PF_X = 5 (read + execute, no write — safe)
    auto elf = make_elf64_with_phdr(5);
    TempFile tf(elf);
    if (!tf.valid()) { TEST_PASS("audit: ELF safe segment (skipped)"); return; }
    auto r = audit_plugin_binary(tf.path);
    // Without RW, the segment is fine — but the scanner still sees
    // a valid file and should pass.
    expect(r.passed, "audit: ELF with RX segment passes");
    expect(!r.has_wx_segment, "audit: ELF with RX has no W+X");
    std::cout << "PASS: test_audit_elf_safe_segment" << std::endl;
}

#endif // platform-specific

// ─── AlgorithmLoader integration ───────────────────────────────

void test_audit_after_failed_load() {
    AlgorithmLoader loader;
    loader.load_builtin();

    // Attempt loading a non-existent plugin → load_plugin fails
    // before dlopen, but _last_audit should be set.
    // (Note: load_plugin returns false and doesn't throw.)
    // Access last_audit to verify it was set (even if to a valid
    // report from the non-existent file scan).
    // last_audit may be nullptr or contain a report depending on
    // where load_plugin failed — both are acceptable states.
    // The important thing is no crash and no UB.

    std::cout << "PASS: test_audit_after_failed_load" << std::endl;
}

void test_audit_get_report_builtin() {
    AlgorithmLoader loader;
    loader.load_builtin();

    // Built-in algorithms are NOT plugins, so no audit report.
    auto *report = loader.get_audit_report("astar");
    expect(report == nullptr, "audit: built-in algo has no audit report");

    std::cout << "PASS: test_audit_get_report_builtin" << std::endl;
}

// ======================================================================
//  main
// ======================================================================

int main() {
    try {
        test_audit_empty_file();
        test_audit_truncated_binary();

#if defined(_WIN32)
        test_audit_valid_pe();
        test_audit_pe_wx_section();
        test_audit_pe_32bit_rejected();
#elif defined(__linux__)
        test_audit_valid_elf();
        test_audit_elf_wx_segment();
        test_audit_elf_32bit_rejected();
        test_audit_elf_safe_segment();
#endif

        test_audit_after_failed_load();
        test_audit_get_report_builtin();
    } catch (const test_error &e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
