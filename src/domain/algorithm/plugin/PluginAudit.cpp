#include "PluginAudit.h"
#include "common/log/log.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ── Platform system headers (outside namespace — C linkage) ────
#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__linux__)
#include <elf.h>
#endif

#if defined(_WIN32)
#include <winnt.h>
#endif

// ======================================================================
//  Common dangerous-symbol database (inside algorithm::)
// ======================================================================

namespace algorithm {
namespace {

// — Network / process / dynamic-code — RED (hard suspicion)
#if defined(__linux__) || defined(_WIN32)
bool is_red_flag(const std::string_view name) noexcept {
    // clang-format off
    static constexpr auto RED = std::to_array<const char *>({
        "socket",     "connect",    "bind",       "listen",     "accept",
        "send",       "recv",       "sendto",     "recvfrom",   "sendmsg",    "recvmsg",
        "setsockopt", "getsockopt",
        "WSAStartup", "WSACleanup", "WSASocketA", "WSASocketW", "WSAConnect",
        "fork",       "vfork",      "execve",     "execvp",     "execl",
        "execle",     "execv",      "execlp",
        "system",     "popen",      "pclose",
        "clone",      "clone3",
        "dlopen",     "dlsym",      "dlclose",    "dlmopen",
        "LoadLibraryA",  "LoadLibraryW",  "LoadLibraryExA",  "LoadLibraryExW",
        "GetProcAddress",
        "mprotect",   "pkey_mprotect",
        "ptrace",
        "CreateProcessA", "CreateProcessW", "WinExec",
        "ShellExecuteA",  "ShellExecuteW",
        // ── Filesystem access — forge plugins are pure computation, ANY file
        // access is suspicious (was a blind spot: the malicious plugin's
        // open("/etc/passwd") imported `open` but it wasn't flagged). ──
        "open",       "openat",     "creat",      "unlink",     "unlinkat",
        "rename",     "renameat",   "mkdir",      "mkdirat",    "rmdir",
        "chmod",      "fchmod",     "chown",      "fchown",     "truncate",
        "ftruncate",  "access",     "readlink",   "symlink",    "link",
        "mknod",      "remove",
        "CreateFileA", "CreateFileW", "CreateFile2", "DeleteFileA", "DeleteFileW",
        "MoveFileA",   "MoveFileW",   "CopyFileA",   "CopyFileW",
        "RemoveDirectoryA", "RemoveDirectoryW", "SetFileAttributesA",
        "SetFileAttributesW", "CreateDirectoryA", "CreateDirectoryW",
        // ── High-level file-open entry points — pure-compute plugins need
        // none; fopen was previously whitelisted as a "runtime" symbol, an
        // asymmetry with open() that let stdio file access sail through. ──
        "fopen",  "fopen_s", "_wfopen",
        // ── Windows file I/O / mapping — plugins do their I/O via the worker's
        // IPC channel, never direct file APIs ──
        "ReadFile", "WriteFile", "CreateFileMappingA", "CreateFileMappingW",
        "MapViewOfFile", "UnmapViewOfFile",
        // ── NT API aliases ──
        "NtCreateFile", "ZwCreateFile",
        // ── Windows memory / thread control (executable-memory & injection) ──
        "VirtualAlloc", "VirtualProtect", "CreateThread",
        // ── Registry reads too (was write-only before) ──
        "RegQueryValueExA", "RegQueryValueExW",
        // ── Network resolvers — DNS is network activity even without connect ──
        "getaddrinfo", "gethostbyname", "getnameinfo", "gethostbyaddr",
        "InternetOpenA", "InternetOpenW", "InternetConnectA", "InternetConnectW",
        "WinHttpOpen", "URLDownloadToFileA", "URLDownloadToFileW",
        // ── Code/process injection & cross-process memory ──
        "memfd_create", "process_vm_writev", "process_vm_readv",
        "bpf", "userfaultfd",
        "CreateRemoteThread", "WriteProcessMemory", "ReadProcessMemory",
        "VirtualAllocEx", "VirtualProtectEx",
        // ── Registry (Windows) ──
        "RegOpenKeyExA", "RegOpenKeyExW", "RegCreateKeyExA", "RegCreateKeyExW",
        "RegSetValueExA", "RegSetValueExW", "RegDeleteKeyA", "RegDeleteKeyW",
        // ── Windows process control ──
        "CreateProcessWithTokenA", "CreateProcessWithTokenW",
        "CreateProcessAsUserA", "CreateProcessAsUserW",
        "TerminateProcess",
        // ── Linux process / kernel interface ──
        "syscall", "prctl", "unshare", "personality",
    });
    // clang-format on
    return std::ranges::any_of(RED, [name](const char* f) { return name == f; });
}
#endif

// — Standard BESQ exports — filter out of extra_exports
static bool is_besq_export(const std::string_view name) noexcept {
    return name == BESQ_PLUGIN_CREATE_SYM || name == BESQ_PLUGIN_CAPABILITY_SYM;
}

// — Common C/C++ runtime symbols — NOT suspicious, filter out of imports
// Only used by the ELF scanner (Linux).
#if defined(__linux__)

/// Common ELF linker symbols — compiler noise, not plugin API surface.
static bool is_linker_symbol(const std::string_view name) noexcept {
    return name == "_init" || name == "_fini" || name == "__bss_start" || name == "_edata" || name == "_end";
}

bool is_runtime_sym(const std::string_view name) noexcept {
    // Skip compiler-internal / ELF-local symbols
    if (name.starts_with('_') || name.starts_with('.'))
        return true;
    // clang-format off
    static constexpr auto RUNTIME = std::to_array<const char *>({
        "malloc",  "free",    "calloc",  "realloc",
        "aligned_alloc", "posix_memalign",
        "memcpy",  "memmove", "memset",  "memcmp",   "memchr",
        "strlen",  "strcpy",  "strcmp",  "strncmp",  "strncpy", "strchr", "strrchr",
        "printf",  "fprintf", "sprintf", "snprintf", "asprintf",
        "puts",    "fputs",   "putchar", "fputc",    "fwrite",  "fread",
        "fclose",  "fflush",   // fopen is RED (file-open entry point)
        "exit",    "abort",   "atexit",  "quick_exit", "_Exit",
        "__assert_fail", "__cxa_assert_fail",
        "write",   "read",
        "mmap",    "munmap",  "brk",     "sbrk",
        "close",
        "rand",    "srand",   "time",
        "clock_gettime", "gettimeofday",
    });
    // clang-format on
    return std::ranges::any_of(RUNTIME, [name](const char* r) { return name == r; });
}
#endif

// ======================================================================
//  MappedFile — cross-platform file memory-mapping helper
// ======================================================================

#if defined(__linux__)

struct MappedFile {
    int fd{-1};
    void* data{nullptr};
    size_t size{0};

    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool map(const std::string& path) {
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0)
            return false;
        struct stat st;
        if (::fstat(fd, &st) < 0) {
            ::close(fd);
            fd = -1;
            return false;
        }
        size = static_cast<size_t>(st.st_size);
        if (size == 0) {
            ::close(fd);
            fd = -1;
            return false;
        }
        data = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            ::close(fd);
            fd = -1;
            data = nullptr;
            return false;
        }
        return true;
    }

    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept { return {static_cast<const uint8_t*>(data), size}; }
};

MappedFile::~MappedFile() {
    if (data)
        ::munmap(data, size);
    if (fd >= 0)
        ::close(fd);
}

#elif defined(_WIN32)

struct MappedFile {
    HANDLE hFile{INVALID_HANDLE_VALUE};
    HANDLE hMap{nullptr};
    void* data{nullptr};
    size_t size{0};

    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool map(const std::string& path) {
        hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER li{};
        if (!GetFileSizeEx(hFile, &li)) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        size = static_cast<size_t>(li.QuadPart);
        if (size == 0) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMap) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
        if (!data) {
            CloseHandle(hMap);
            hMap = nullptr;
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        return true;
    }

    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept { return {static_cast<const uint8_t*>(data), size}; }
};

MappedFile::~MappedFile() {
    if (data)
        UnmapViewOfFile(data);
    if (hMap)
        CloseHandle(hMap);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
}

#endif

// ======================================================================
//  ELF scanner  (Linux)
// ======================================================================

#if defined(__linux__)

static PluginAuditReport audit_elf(const std::string& path) {
    PluginAuditReport report;
    MappedFile mf;
    if (!mf.map(path)) {
        LOG_WARN("[Audit] Cannot open '%s' for scanning", path.c_str());
        report.passed = false;
        return report;
    }

    const auto file = mf.bytes();
    const auto* base = file.data();
    const size_t fsize = file.size();

    // ── Verify ELF magic & 64-bit ───────────────────────────────────
    if (fsize < EI_NIDENT || memcmp(base, ELFMAG, SELFMAG) != 0) {
        LOG_WARN("[Audit] '%s' is not an ELF file", path.c_str());
        report.passed = false;
        return report;
    }
    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(base);
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        LOG_WARN("[Audit] '%s' is not 64-bit ELF", path.c_str());
        report.passed = false;
        return report;
    }
    // Structures are interpreted in host byte order — refuse anything but
    // little-endian (the only order the supported toolchains emit).
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        LOG_WARN("[Audit] '%s' is not little-endian ELF — unsupported", path.c_str());
        report.passed = false;
        return report;
    }

    // ── Program headers → W^X check ─────────────────────────────────
    if (ehdr->e_phoff + static_cast<uint64_t>(ehdr->e_phnum) * sizeof(Elf64_Phdr) > fsize) {
        LOG_WARN("[Audit] '%s' has truncated program headers", path.c_str());
        report.passed = false;
        return report;
    }
    const auto* phdr = reinterpret_cast<const Elf64_Phdr*>(base + ehdr->e_phoff);
    for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_W) && (phdr[i].p_flags & PF_X)) {
            report.has_wx_segment = true;
            report.passed = false;
            break;
        }
    }

    // ── Section headers ─────────────────────────────────────────────
    if (ehdr->e_shoff == 0 || ehdr->e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr->e_shoff + static_cast<uint64_t>(ehdr->e_shnum) * sizeof(Elf64_Shdr) > fsize) {
        // Without section headers we cannot audit imports/exports.  Structurally
        // valid but OPAQUE — mark `limited`; the loader refuses opaque plugins
        // without a sandbox and permits them when contained (like a dangerous
        // import).  Not a hard `passed=false`: the file may be a legitimate
        // section-less ELF.
        LOG_WARN("[Audit] '%s' has no valid section headers — audit limited", path.c_str());
        report.limited = true;
        return report;
    }
    const auto* shdr = reinterpret_cast<const Elf64_Shdr*>(base + ehdr->e_shoff);

    const Elf64_Shdr* dynsym_sec = nullptr;
    const Elf64_Shdr* dynstr_sec = nullptr; // string table for .dynsym
    const Elf64_Shdr* dynamic_sec = nullptr;
    const Elf64_Shdr* dynstr_dyn = nullptr; // string table for .dynamic (DT_NEEDED)

    for (Elf64_Half i = 0; i < ehdr->e_shnum; ++i) {
        switch (shdr[i].sh_type) {
        case SHT_DYNSYM:
            dynsym_sec = &shdr[i];
            if (shdr[i].sh_link < static_cast<decltype(shdr[i].sh_link)>(ehdr->e_shnum))
                dynstr_sec = &shdr[shdr[i].sh_link];
            break;
        case SHT_DYNAMIC:
            dynamic_sec = &shdr[i];
            if (shdr[i].sh_link < static_cast<decltype(shdr[i].sh_link)>(ehdr->e_shnum))
                dynstr_dyn = &shdr[shdr[i].sh_link];
            break;
        }
    }

    if (!dynsym_sec || !dynstr_sec) {
        // No dynamic symbols → imports/exports invisible to the audit.  Mark
        // `limited` (opaque), same loader policy as above.
        LOG_WARN("[Audit] '%s' has no dynamic symbol table — audit limited", path.c_str());
        report.limited = true;
        return report;
    }

    // ── Parse .dynsym → exports + imports ──────────────────────────
    const size_t sym_count = dynsym_sec->sh_size / sizeof(Elf64_Sym);
    if (dynsym_sec->sh_offset + dynsym_sec->sh_size > fsize) {
        LOG_WARN("[Audit] '%s' has truncated .dynsym", path.c_str());
        return report;
    }
    const auto* syms = reinterpret_cast<const Elf64_Sym*>(base + dynsym_sec->sh_offset);
    // ── Validate .dynstr section fits in file before dereferencing ──
    if (dynstr_sec->sh_offset + dynstr_sec->sh_size > fsize) {
        LOG_WARN("[Audit] '%s' has truncated .dynstr", path.c_str());
        return report;
    }
    const auto* strtab = reinterpret_cast<const char*>(base + dynstr_sec->sh_offset);

    for (size_t i = 0; i < sym_count; ++i) {
        if (syms[i].st_name >= dynstr_sec->sh_size)
            continue;
        // Bounded symbol name: a malicious .dynstr need not be NUL-terminated,
        // and a raw const char* would let strlen() run past the section (and
        // possibly the whole mmap) — a scanner self-DoS.  Bound by section end.
        const char* sym_name = strtab + syms[i].st_name;
        const size_t name_len = strnlen(sym_name, dynstr_sec->sh_size - syms[i].st_name);
        if (name_len == 0)
            continue;
        const std::string_view name(sym_name, name_len);

        const unsigned char bind = ELF64_ST_BIND(syms[i].st_info);
        const unsigned char type = ELF64_ST_TYPE(syms[i].st_info);

        if (syms[i].st_shndx == SHN_UNDEF) {
            // — Imported (undefined) symbol —
            if (type != STT_FUNC && type != STT_NOTYPE)
                continue;
            if (is_runtime_sym(name))
                continue;
            if (is_red_flag(name))
                report.dangerous_imports.emplace_back(std::string(name));
        } else if (bind == STB_GLOBAL) {
            // — Exported (defined, global) symbol —
            if (type != STT_FUNC && type != STT_NOTYPE)
                continue;
            if (is_besq_export(name) || is_linker_symbol(name))
                continue;
            report.extra_exports.emplace_back(std::string(name));
        }
    }

    // ── Parse .dynamic → DT_NEEDED libraries ──────────────────────
    if (dynamic_sec && dynstr_dyn && dynamic_sec->sh_offset + dynamic_sec->sh_size <= fsize &&
        dynstr_dyn->sh_offset + dynstr_dyn->sh_size <= fsize) {

        const size_t n_dyn = dynamic_sec->sh_size / sizeof(Elf64_Dyn);
        const auto* dyn = reinterpret_cast<const Elf64_Dyn*>(base + dynamic_sec->sh_offset);
        const auto* dynstr_base = reinterpret_cast<const char*>(base + dynstr_dyn->sh_offset);

        for (size_t i = 0; i < n_dyn; ++i) {
            if (dyn[i].d_tag == DT_NEEDED) {
                const uint64_t off = dyn[i].d_un.d_val;
                if (off < dynstr_dyn->sh_size) {
                    const char* lib = dynstr_base + off;
                    const size_t lib_len = strnlen(lib, dynstr_dyn->sh_size - off);
                    if (lib_len > 0)
                        report.linked_libraries.emplace_back(lib, lib_len);
                }
            }
        }
    }

    return report;
}

#endif // __linux__

// ======================================================================
//  PE scanner  (Windows)
// ======================================================================

#if defined(_WIN32)

// Convert RVA to file offset using the section table.
static DWORD rva_to_offset(const IMAGE_NT_HEADERS64* nt, const uint8_t* base, DWORD rva) {
    (void)base;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        // Use the LARGER of VirtualSize / SizeOfRawData: sections like .bss have
        // VirtualSize > SizeOfRawData, and data directories may legally point
        // into that tail.  64-bit math avoids 32-bit overflow.
        const uint64_t va = sec[i].VirtualAddress;
        const uint64_t sz = std::max<uint64_t>(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData);
        if (rva >= va && rva < va + sz)
            return static_cast<DWORD>(rva - va + sec[i].PointerToRawData);
    }
    return 0;
}

static PluginAuditReport audit_pe(const std::string& path) {
    PluginAuditReport report;
    MappedFile mf;
    if (!mf.map(path)) {
        LOG_WARN("[Audit] Cannot open '%s' for scanning", path.c_str());
        report.passed = false;
        return report;
    }

    const auto file = mf.bytes();
    const auto* base = file.data();
    const size_t fsize = file.size();

    // ── Verify DOS header & NT signature ───────────────────────────
    if (fsize < sizeof(IMAGE_DOS_HEADER)) {
        LOG_WARN("[Audit] '%s' is truncated", path.c_str());
        report.passed = false;
        return report;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        LOG_WARN("[Audit] '%s' is not a PE file (bad DOS magic)", path.c_str());
        report.passed = false;
        return report;
    }

    const LONG nt_offset = dos->e_lfanew;
    if (nt_offset < 0 || static_cast<DWORD>(nt_offset) + sizeof(IMAGE_NT_HEADERS64) > fsize) {
        LOG_WARN("[Audit] '%s' has invalid NT header offset", path.c_str());
        report.passed = false;
        return report;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + nt_offset);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        LOG_WARN("[Audit] '%s' has bad NT signature", path.c_str());
        report.passed = false;
        return report;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        LOG_WARN("[Audit] '%s' is not 64-bit PE", path.c_str());
        report.passed = false;
        return report;
    }

    // ── Section headers → W^X check ───────────────────────────────
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const DWORD chars = sec[i].Characteristics;
        if ((chars & IMAGE_SCN_MEM_WRITE) && (chars & IMAGE_SCN_MEM_EXECUTE)) {
            report.has_wx_segment = true;
            report.passed = false;
            break;
        }
    }

    // Helper: get a usable pointer from a data-directory entry.
    auto data_dir_ptr = [&](int idx, DWORD& out_size) -> const uint8_t* {
        if (idx >= static_cast<int>(nt->OptionalHeader.NumberOfRvaAndSizes))
            return nullptr;
        const DWORD rva = nt->OptionalHeader.DataDirectory[idx].VirtualAddress;
        const DWORD sz = nt->OptionalHeader.DataDirectory[idx].Size;
        out_size = sz;
        if (rva == 0 || sz == 0)
            return nullptr;
        const DWORD offset = rva_to_offset(nt, base, rva);
        if (offset == 0 || static_cast<uint64_t>(offset) + sz > fsize)
            return nullptr;
        return base + offset;
    };

    // ── Export directory ──────────────────────────────────────────
    DWORD export_size = 0;
    const uint8_t* export_base = data_dir_ptr(IMAGE_DIRECTORY_ENTRY_EXPORT, export_size);
    if (export_base && export_size >= sizeof(IMAGE_EXPORT_DIRECTORY)) {
        const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(export_base);

        if (exp->NumberOfNames > 0 && exp->AddressOfNames != 0) {
            const DWORD names_off = rva_to_offset(nt, base, exp->AddressOfNames);
            // `names_off < fsize` is required: without it, (fsize - names_off)
            // underflows to a huge size_t when names_off lies past the file.
            if (names_off != 0 && names_off < fsize) {
                const auto* name_ptrs = reinterpret_cast<const DWORD*>(base + names_off);
                // Bound by remaining file size (not export directory bounds —
                // AddressOfNames may point outside the export directory).
                const size_t max_names = (fsize - names_off) / sizeof(DWORD);

                const DWORD limit = (max_names < exp->NumberOfNames) ? static_cast<DWORD>(max_names) : exp->NumberOfNames;
                for (DWORD i = 0; i < limit; ++i) {
                    const DWORD name_off = rva_to_offset(nt, base, name_ptrs[i]);
                    if (name_off == 0 || name_off >= fsize)
                        continue;

                    const char* name = reinterpret_cast<const char*>(base + name_off);
                    const size_t avail = fsize - name_off;
                    const size_t len = strnlen(name, avail);
                    if (len == 0 || len >= avail)
                        continue;

                    if (is_besq_export(name))
                        continue;
                    report.extra_exports.emplace_back(name, len);
                }
            }
        }
    }

    // ── Import directory ──────────────────────────────────────────
    DWORD import_size = 0;
    const uint8_t* import_base = data_dir_ptr(IMAGE_DIRECTORY_ENTRY_IMPORT, import_size);
    if (import_base && import_size >= sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* imp_desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(import_base);
        const size_t max_descs = import_size / sizeof(IMAGE_IMPORT_DESCRIPTOR);

        for (size_t d = 0; d < max_descs; ++d) {
            if (imp_desc[d].OriginalFirstThunk == 0 && imp_desc[d].FirstThunk == 0)
                break;

            // DLL name (bounded: a malicious PE need not NUL-terminate it)
            const DWORD name_off = rva_to_offset(nt, base, imp_desc[d].Name);
            if (name_off == 0 || name_off >= fsize)
                continue;
            const char* dll_name = reinterpret_cast<const char*>(base + name_off);
            const size_t dll_len = strnlen(dll_name, fsize - name_off);
            if (dll_len == 0 || dll_len >= fsize - name_off)
                continue;
            const std::string_view dll_view(dll_name, dll_len);
            report.linked_libraries.emplace_back(dll_view);

            // Thunk table
            const DWORD thunk_rva = imp_desc[d].OriginalFirstThunk ? imp_desc[d].OriginalFirstThunk : imp_desc[d].FirstThunk;
            const DWORD thunk_off = rva_to_offset(nt, base, thunk_rva);
            if (thunk_off == 0 || thunk_off + sizeof(IMAGE_THUNK_DATA64) > fsize)
                continue;

            // Bound the thunk walk by file size: a malicious PE can omit the
            // null terminator and drive an unbounded walk past the mapping.
            const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + thunk_off);
            const size_t max_thunks = (fsize - thunk_off) / sizeof(IMAGE_THUNK_DATA64);
            for (size_t ti = 0; ti < max_thunks; ++ti) {
                const auto& t = thunk[ti];
                if (t.u1.AddressOfData == 0)
                    break;
                if (!IMAGE_SNAP_BY_ORDINAL64(t.u1.Ordinal)) {
                    const DWORD iname_off = rva_to_offset(nt, base, static_cast<DWORD>(t.u1.AddressOfData));
                    if (iname_off != 0 && iname_off + sizeof(IMAGE_IMPORT_BY_NAME) <= fsize) {
                        const auto* import_by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + iname_off);
                        const char* func_name = import_by_name->Name;
                        const size_t avail = fsize - iname_off - 2;
                        const size_t flen = strnlen(func_name, avail);
                        // Pass a BOUNDED view to is_red_flag — a raw const char*
                        // would strlen past the mapping if the name isn't NUL-
                        // terminated within the file.
                        if (flen > 0 && flen < avail && is_red_flag(std::string_view(func_name, flen))) {
                            report.dangerous_imports.emplace_back(std::string(dll_view) + "!" + std::string(func_name, flen));
                        }
                    }
                }
            }
        }
    }

    return report;
}

#endif // _WIN32

} // anonymous namespace

// ======================================================================
//  Public API
// ======================================================================

PluginAuditReport audit_plugin_binary(const std::string& so_path) {
#if defined(__linux__)
    return audit_elf(so_path);
#elif defined(_WIN32)
    return audit_pe(so_path);
#elif defined(__APPLE__)
    PluginAuditReport r;
    r.passed = false; // Mach-O scanning not yet implemented
    LOG_WARN("[Audit] Binary scanning not available on macOS: %s", so_path.c_str());
    return r;
#else
    // Unknown platform: no scanner → cannot certify the plugin → fail closed.
    PluginAuditReport r;
    r.passed = false;
    LOG_WARN("[Audit] Binary scanning not available on this platform");
    return r;
#endif
}

} // namespace algorithm
