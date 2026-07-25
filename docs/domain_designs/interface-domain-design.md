# Interface Domain Design

> Version: 2.0
> Last updated: 2026-07-26
> Status: Implemented

---

## 1. Overview

The interface domain is a **stateless translation layer** between the external world and the orchestration/business domains. It parses external input into domain types, delegates work to orchestration pipelines, and returns results.

```
External Input (CLI args / C ABI call)
    │
    ▼
┌─────────────────────────────────────┐
│        Interface Domain             │
│  (Translation + Session Context)    │
│                                     │
│  ┌─────────────────────────────┐    │
│  │  BesqContext                │    │
│  │  (session context holder)   │    │
│  └──────────┬──────────────────┘    │
│             │ delegates             │
│  ┌──────────▼──────────────────┐    │
│  │  CLI Parsers                │    │
│  │  (input → domain types)     │    │
│  └─────────────────────────────┘    │
└──────────────┬──────────────────────┘
               │
               ▼
     Orchestration Domain
     (SolvePipeline / ManagePipeline / ExportPipeline)
```

### Key Characteristics

| Property | Description |
|----------|-------------|
| **Translation only** | No pipeline logic, no formatting, no serialization — all delegated to orchestration |
| **Session context** | `BesqContext` holds per-session state (ProfileManager, AlgorithmLoader) |
| **Business-domain aware** | Uses business domain types (`EnchSet`, `Item`, `Profile`) directly — no parallel type hierarchy |
| **No algorithm coupling** | Never depends on algorithm domain internals |
| **No formatting** | Output formatting (`OutputFormatter`) and registry serialization (`EnchSerializer`) live in orchestration |

---

## 2. Directory Structure

```
src/domain/interface/
├── interface.h                       ← Umbrella header
├── CMakeLists.txt
├── BesqContext.cpp                   ← Application session facade
│
├── cli/                              ← CLI module
│   ├── cli.h/cpp                     CLIConfig, parse_cli(), apply_config_pairs()
│   ├── CLIParser.h/cpp               Generic --key=value argument parser
│   ├── EnchParser.h/cpp              "sharpness=5" → EnchSet (registry-aware)
│   ├── ItemParser.h/cpp              "diamond_sword[...]" → Item (registry-aware)
│   └── RegistryEditor.h/cpp          --registry-edit operations
│
└── abi/
    └── CAbiBindings.cpp              C ABI implementation
```

### What Is Not Here

| Removed | Reason |
|---------|--------|
| `types/SpecTypes.h` | `EnchantmentSpec`/`TargetSpec` eliminated — parsers now produce business types directly |
| `SolvePipeline.h/cpp` | Moved to `orchestration/pipelines/` |
| `protocol/SolveRequest.h` | Not created — protocol types live in `orchestration/types/` |
| `protocol/SolveResponse.h` | Same as above |
| `components/ParserUtilsDomain.hpp` | Absorbed by business domain (FormatDetector) + common utils |
| `fs/FileFormat.h` | Replaced by `business/components/FormatDetector` |

---

## 3. Component Details

### 3.1 BesqContext — Session Context

Holds per-session state and delegates all operations to orchestration pipelines.

```cpp
// include/besq/besq.h
class BesqContext {
public:
    BesqContext();
    ~BesqContext();

    // Non-copyable, movable
    BesqContext(const BesqContext&) = delete;
    BesqContext& operator=(const BesqContext&) = delete;
    BesqContext(BesqContext&&) noexcept;
    BesqContext& operator=(BesqContext&&) noexcept;

    // ── Solve ──
    // Translate → delegate → return
    orchestration::SolveResult solve(const orchestration::SolveRequest& input);

    // ── Profile lifecycle (delegated to ManagePipeline) ──
    void load_builtin();
    void load_file(const std::string& path);
    const std::string& active_profile() const noexcept;
    std::vector<std::string> list_profiles() const;
    void activate_profile(const std::string& name);
    void fork_profile(const std::string& source, const std::string& dest);
    void merge_profile(const std::string& source, const std::string& dest);
    void remove_profile(const std::string& name);

    // ── Registry editing (delegated to ManagePipeline) ──
    bool add_enchantment(const EnchInfo& info);
    bool remove_enchantment(const std::string& name_id);
    bool modify_enchantment(const std::string& name_id, const EnchInfo& patch);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const std::string& name_id);
    bool add_category(const std::string& name);

    // ── Registry access (read-only) ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const EquipmentTagRegistry& categories() const noexcept;

    // ── Persistence ──
    bool export_registry(const std::string& path) const;

    // ── Algorithm ──
    size_t load_algorithms(const std::string& dir_path);
    std::vector<std::string> list_algorithms() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
```

**Implementation pattern:**
```cpp
orchestration::SolveResult BesqContext::solve(const orchestration::SolveRequest& input) {
    auto& profile = _impl->profiles.active();
    return orchestration::SolvePipeline::run(profile, input, _impl->algo_loader);
}
```

### 3.2 CLI Parsers

All parsers are **registry-aware** — they resolve string IDs to business domain types at parse time. No intermediate `SpecTypes` layer.

**EnchParser** — `"sharpness=5,knockback=2"` → `EnchSet`:

```cpp
struct EnchParser {
    static EnchSet parse(
        const std::string& input,
        const EnchantmentRegistry& ench_reg
    );
};
```

**ItemParser** — `"diamond_sword[sharpness=5]"` → `Item`:

```cpp
struct ItemParser {
    static Item parse(
        const std::string& input,
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& eq_reg
    );
};
```

**Key change from v1:**
- `EnchantmentSpec`/`TargetSpec` eliminated
- `build_target()` / `build_enchset()` in cli.h eliminated
- Parsers resolve NSIDs directly via registries, same logic previously in the builder helpers

**CLIParser** — Generic `--key=value` parser, zero business knowledge:

```cpp
struct CLIParser {
    struct ParsedArg { std::string key; std::string value; };
    static std::vector<ParsedArg> parse(int argc, char* argv[]);
};
```

**cli.h/cpp** — Business-aware CLI layer:

```cpp
struct CLIConfig {
    std::string algorithm = "hamming";
    std::string mode      = "direct";
    std::string target;
    std::string source;
    std::string config_pairs;
    // ... other fields
};

CLIConfig parse_cli(int argc, char* argv[]);
std::string get_cli_help_text(const std::string& program_name = "besq");
void apply_config_pairs(const std::string& config_pairs, algorithm::ForgeConfig& cfg);
```

### 3.3 C ABI

Thin wrapper over `BesqContext` methods. JSON interchange for complex types. Unchanged in structure — only updates types used:

```cpp
// besq_solve now returns JSON built from SolveResult (no SolveResponse::to_json())
char* besq_solve(BesqContext* ctx, const char* json_input);
```

---

## 4. Extension Module Pattern

Every extension module follows:

```
External Input
    → Module-specific Parsing    (e.g., CLI args → domain types)
    → Assemble SolveRequest      (orchestration types)
    → BesqContext::solve()       (delegate to orchestration)
    → Format Output              (use OutputFormatter from orchestration)
```

Current modules:

| Module | Status | Input | Output |
|--------|--------|-------|--------|
| CLI | P1 (active) | CLI args | Text/JSON via OutputFormatter |
| C ABI | P1 (active) | C structs | JSON string |

---

## 5. Cross-Domain Dependencies

```
interface/
├── → business/              (EnchSet, Item, Profile, registries)
├── → orchestration/         (SolvePipeline, ManagePipeline, ExportPipeline)
├── → algorithm/             (ForgeConfig, SearchConfig — via protocol types)
└── → common/                (utilities, logging)
```

### What Interface Does NOT Own

| Concern | Owner |
|---------|-------|
| Solve pipeline | `orchestration/pipelines/SolvePipeline` |
| Profile/registry management | `orchestration/pipelines/ManagePipeline` |
| Data export | `orchestration/pipelines/ExportPipeline` |
| Type conversion (business ↔ compact) | `orchestration/components/CompactAdapter` |
| Solution formatting | `orchestration/components/OutputFormatter` |
| Registry serialization | `orchestration/components/EnchSerializer` |
| Game data types | `business/types/` |
| Data file parsing | `business/parsers/` |
| Format detection | `business/components/FormatDetector` |
| Profile management | `business/managers/ProfileManager` |

---

## 6. Implementation Status

All four phases are **completed** (2026-07-26).

| # | Task | Status | Description |
|---|------|--------|-------------|
| **S1** | Registry-aware parsers | ✅ | EnchParser returns `EnchSet` (registry-aware), ItemParser returns `Item`, `SpecTypes.h` deleted, `build_target()`/`build_enchset()` removed |
| **S2** | Move parsers to `cli/` | ✅ | `CLIParser`, `EnchParser`, `ItemParser` from `parsers/` → `cli/`, all includes and CMake updated |
| **S3** | Cleanup | ✅ | `ParserUtilsDomain.hpp` and `FileFormat.h` deleted; utility functions inlined to `EnchSerializer.cpp` |
| **S4** | Finalize | ✅ | `interface.h` and `CMakeLists.txt` reflect final structure |

### Final Directory Structure

```
src/domain/interface/
├── interface.h                       ← Umbrella header
├── CMakeLists.txt
├── BesqContext.cpp                   ← Session context
├── cli/                              ← CLI module
│   ├── cli.h/cpp                     CLIConfig, parse_cli(), apply_config_pairs()
│   ├── CLIParser.h/cpp               Generic --key=value argument parser
│   ├── EnchParser.h/cpp              "sharpness=5" → EnchSet (registry-aware)
│   ├── ItemParser.h/cpp              "diamond_sword[...]" → Item (registry-aware)
│   └── RegistryEditor.h/cpp          --registry-edit operations
└── abi/
    └── CAbiBindings.cpp              C ABI implementation
```
