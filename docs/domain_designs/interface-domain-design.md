# Interface Domain Design

> Version: 1.0
> Last updated: 2026-07-25
> Status: Draft — Pending Implementation

---

## Table of Contents

1. [Overview](#1-overview)
2. [Core Principles](#2-core-principles)
3. [Architecture](#3-architecture)
4. [Directory Structure](#4-directory-structure)
5. [Protocol Layer](#5-protocol-layer)
6. [Extension Modules](#6-extension-modules)
7. [Public API](#7-public-api)
8. [Cross-Domain Relations](#8-cross-domain-relations)
9. [Implementation Plan](#9-implementation-plan)

---

## 1. Overview

The interface domain (`src/domain/interface/`) is a **stateless data service interface** that translates external data and requests into system-compatible data and tasks, then delegates execution to the orchestration pipeline.

It acts as the boundary between the external world and the system's internal domains:

```
External World
    │
    ▼
┌──────────────────────────────────────┐
│         Interface Domain             │
│  (Stateless Data Service Interface)  │
│                                      │
│  ┌──────────┐  ┌──────┐  ┌───────┐  │
│  │   CLI    │  │ ABI  │  │ Web   │  │  ← Extension modules
│  │  (P1)    │  │ (P1) │  │ (P3)  │  │
│  └────┬─────┘  └──┬───┘  └───┬───┘  │
│       │           │          │      │
│  ┌────▼───────────▼──────────▼───┐  │
│  │        Protocol Layer         │  │
│  │  (SolveRequest/Response)      │  │
│  └────────────┬──────────────────┘  │
│               │                     │
│  ┌────────────▼──────────────────┐  │
│  │        Public API             │  │
│  │  (BesqContext + Pipeline)     │  │
│  └────────────┬──────────────────┘  │
└───────────────┼─────────────────────┘
                │ delegates
                ▼
      Orchestration Domain
```

### Key Characteristics

| Property | Description |
|----------|-------------|
| **Stateless** | Interface components are pure transformers — no mutable state at the module level. The only state lives in `BesqContext` (application context), which is managed at the top level and delegates all business logic to the business domain. |
| **Pluggable** | Extension modules (CLI, ABI, TUI, GUI, Web API) are independent and follow the same pattern: translate external input → protocol → call pipeline → translate output. |
| **Business-domain aware** | Uses business domain types (`Item`, `EnchSet`, `Solution`, `Profile`) as its canonical data model. Does not define parallel data types. |
| **No algorithm coupling** | Interface never depends on algorithm domain internals. Algorithm concerns are accessed through orchestration. |

---

## 2. Core Principles

### 2.1 Extension Module Pattern

Every extension module follows a uniform pipeline:

```
External Input
    → Module-specific Parsing    (e.g., CLI args, C struct, HTTP JSON)
    → Protocol Assembly           (build SolveRequest)
    → BesqContext::solve()        (delegate to pipeline)
    → Protocol -> Output Format   (e.g., text table, JSON, stdout)
```

### 2.2 Protocol as Contract

The `protocol/` layer defines the formal contracts between extension modules and the pipeline:

- **`SolveRequest`** — what information is needed to start a solve
- **`SolveResponse`** — what information is returned after a solve

Extension modules produce `SolveRequest` and consume `SolveResponse`. They never interact with the pipeline internals directly.

### 2.3 Business Domain Alignment

The interface domain **borrows** business domain types rather than defining its own:

| Concept | Type Source |
|---------|-------------|
| Enchantment definition | `business::EnchInfo` |
| Equipment definition | `business::Equipment` |
| Item stack | `business::Item` |
| Enchantment set | `business::EnchSet` |
| Solution | `business::Solution` |
| Profile | `business::Profile` |
| Registry access | `business::*Registry` |

Only CLI-level spec types (`EnchantmentSpec`, `TargetSpec`) are defined in the interface domain because they represent raw CLI input format, not domain concepts.

### 2.4 Stateless Transformation

Interface functions are stateless:

```cpp
// ❌ Not: stateful module that tracks sessions
// ✅ Yes: pure transformation
SolveResponse solve(const SolveRequest& req, const Profile& profile);
```

The only exception is `BesqContext`, which owns the application lifecycle (`ProfileManager`, `AlgorithmLoader`). This is a convenience facade, not domain logic.

---

## 3. Architecture

### Data Flow

```
                    ┌─────────────────────┐
                    │   Extension Module   │
                    │  (CLI / ABI / Web)   │
                    └────────┬────────────┘
                             │
                     ┌───────▼────────┐
                     │  Parse Input   │
                     │  → SpecTypes   │
                     └───────┬────────┘
                             │
                     ┌───────▼────────┐
                     │  Build Request │
                     │  → SolveRequest│
                     └───────┬────────┘
                             │
              ┌──────────────▼──────────────┐
              │         SolvePipeline       │
              │  (protocol → orchestration) │
              └──────────────┬──────────────┘
                             │
                     ┌───────▼────────┐
                     │  Build Response│
                     │  → SolveResult │
                     └───────┬────────┘
                             │
                     ┌───────▼────────┐
                     │  Format Output │
                     │  (text / JSON) │
                     └────────────────┘
```

### Module Dependency

```
interface/
├── protocol/           → depends on common/ + business/types/
├── api/                → depends on protocol/ + business/ + orchestration/ + algorithm/plugin
├── cli/                → depends on protocol/ + business/ + common/
├── abi/                → depends on api/ (BesqContext via besq.h)
```

---

## 4. Directory Structure

```
src/domain/interface/
├── interface.h                         ← Umbrella header
│
├── protocol/                           ← Data exchange contracts (Phase 1)
│   ├── SolveRequest.h                  Solve request specification
│   └── SolveResponse.h                 Solve result specification
│
├── api/                                ← Public C++ API
│   ├── BesqContext.h/cpp              (declaration in include/besq/besq.h)
│   └── SolvePipeline.h/cpp            Pipeline orchestration
│
├── cli/                                ← Extension: CLI (P1)
│   ├── cli.h/cpp                       CLIConfig, parse_cli(), helpers
│   ├── RegistryEditor.h/cpp            --registry-edit operations
│   ├── CLIParser.h/cpp                 Generic key-value argument parser
│   ├── EnchParser.h/cpp                Enchantment spec string parser
│   ├── ItemParser.h/cpp                Target spec string parser
│   └── README.md
│
├── abi/                                ← Extension: C ABI (P1)
│   └── CAbiBindings.cpp                C API implementation
│
└── types/                              ← Interface-only types
    └── SpecTypes.h                     EnchantmentSpec, TargetSpec
```

### File Change Summary (from current state)

| Action | Path | Notes |
|--------|------|-------|
| **Keep** | `cli/cli.h/cpp` | CLI config + business-aware helpers |
| **Keep** | `cli/RegistryEditor.h/cpp` | Registry edit operations |
| **Keep** | `cli/CLIParser.h/cpp` → move under `cli/` | Generic key-value parser |
| **Keep** | `cli/EnchParser.h/cpp` → move under `cli/` | Enchantment spec parser |
| **Keep** | `cli/ItemParser.h/cpp` → move under `cli/` | Target spec parser |
| **Keep** | `abi/CAbiBindings.cpp` | C ABI implementation |
| **Keep** | `BesqContext.cpp` | Already refactored for business domain |
| **Keep** | `SolvePipeline.h/cpp` | Pipeline orchestration |
| **Keep** | `types/SpecTypes.h` | CLI spec types |
| **Remove** | `components/ParserUtilsDomain.hpp` | Absorbed by business domain (FormatDetector) + orchestration (platform string utils) |
| **Remove** | `fs/FileFormat.h` | Business domain owns FormatDetector |
| **Add** | `protocol/SolveRequest.h` | Extract + formalize from SolvePipeline.h |
| **Add** | `protocol/SolveResponse.h` | Extract + formalize from SolvePipeline.h |
| **Update** | `interface.h` | New umbrella structure |
| **Update** | `CMakeLists.txt` | Reflect new structure |

---

## 5. Protocol Layer

### 5.1 SolveRequest

The unified request contract for initiating a solve. All extension modules produce this.

```cpp
// protocol/SolveRequest.h
#pragma once
#include "domain/business/types/Item.h"
#include "domain/business/types/EnchSet.h"
#include "domain/algorithm/types/ConfigTypes.h"

struct SolveRequest {
    Item target_item;                       ///< Target equipment with desired enchants
    EnchSet source_enchantments;            ///< Enchantments already applied
    algorithm::ForgeConfig forge_config;    ///< Forge behaviour flags
    algorithm::SearchConfig search_config;  ///< Search limits
    std::string algorithm = "hamming";      ///< Algorithm strategy name
    std::vector<Item> extra_items;          ///< Additional items (inventory mode)
    std::vector<int32_t> extra_item_priorities; ///< Priority per extra item
    bool is_inventory_mode = false;         ///< Whether extra_items replaces book generation
};
```

This is semantically equivalent to the current `SolveInput` but lives in the protocol namespace to emphasize its role as the formal contract between extension modules and the pipeline.

### 5.2 SolveResponse

The unified result contract. All extension modules consume this.

```cpp
// protocol/SolveResponse.h
#pragma once
#include "domain/business/types/Solution.h"
#include <string>
#include <vector>

struct SolveResponse {
    bool success = false;
    std::vector<Solution> solutions;
    std::string algorithm_used;
    int64_t computation_time_ms = 0;

    // Format helpers
    std::string to_json(const business::EnchantmentRegistry& ench_reg,
                        const business::EquipmentTagRegistry& cat_reg) const;
    std::string to_text(const business::EnchantmentRegistry& ench_reg,
                        const business::EquipmentTagRegistry& cat_reg) const;
    std::string to_json_raw() const;
};
```

### 5.3 Design Notes

- Protocol types are **thin wrappers** — they borrow business domain types directly rather than creating parallel hierarchies.
- Serialization methods on `SolveResponse` require registry references for name resolution. This coupling is acceptable because formatting is inherently registry-aware.
- Extension modules that need custom input/output formats (e.g., Web API returning only raw JSON) can use `to_json_raw()` which is registry-independent.

---

## 6. Extension Modules

### 6.1 CLI — Command Line Interface (P1)

**Purpose**: Parse command-line arguments, interact with the user, and display results.

**Component Overview**:

| Component | Responsibility |
|-----------|---------------|
| `CLIParser` | Generic `--key=value` argument parser. Zero business knowledge. |
| `EnchParser` | Parse `"sharpness=5,knockback=2"` → `EnchantmentSpec[]` |
| `ItemParser` | Parse `"diamond_sword[sharpness=5]"` → `TargetSpec` |
| `cli.h/cpp` | Business-aware layer: `CLIConfig`, `parse_cli()`, `build_target()`, `build_enchset()`, `apply_config_pairs()` |
| `RegistryEditor` | Parse and apply `--registry-edit` operations |

**Data Flow**:
```
argv[]
  → CLIParser::parse()           → ParsedArg[]
  → parse_cli()                  → CLIConfig (validated)
  → EnchParser/ItemParser        → EnchantmentSpec[] / TargetSpec
  → build_target/build_enchset   → Item / EnchSet (domain types)
  → SolveRequest                 → SolvePipeline
```

### 6.2 ABI — C Language Bindings (P1)

**Purpose**: Expose the public API via C linkage for FFI consumers (Python, Rust, etc.).

**Location**: `abi/CAbiBindings.cpp` (implementation), `include/besq/besq_abi.h` (public header)

**Design**: Thin wrapper over `BesqContext` methods. JSON is used as the data interchange format for complex types (enchantment definitions, solve input/output).

### 6.3 Future Modules

| Module | Phase | Input | Output | Notes |
|--------|-------|-------|--------|-------|
| **TUI** | P2 | Terminal events | ANSI/Unicode | Interactive enchantment table simulation |
| **GUI** | P2 | Mouse/Keyboard | Qt/WxWidgets? | Visual forge planner |
| **Web API** | P3 | HTTP + JSON | HTTP + JSON | REST or WebSocket for external tools |

Each future module follows the same pattern:
```
External I/O → Module Handler → SolveRequest → BesqContext::solve() → SolveResponse → Output
```

---

## 7. Public API

### 7.1 BesqContext

BesqContext is the **public entry point** for the entire library. It is stateful (owns ProfileManager + AlgorithmLoader) but delegates all business logic to domain components.

```cpp
class BesqContext {
public:
    // ── Profile lifecycle ──
    void load_builtin();
    size_t load_algorithms(const std::string& dir_path);
    void load_file(const std::string& path);
    void load_data(const std::vector<std::string>& filters);

    // ── Profile management ──
    const std::string& active_profile() const noexcept;
    std::vector<std::string> list_profiles() const;
    void activate_profile(const std::string& name);
    void fork_profile(const std::string& source, const std::string& dest);
    void merge_profile(const std::string& source, const std::string& dest);
    void remove_profile(const std::string& name);

    // ── Registry editing ──
    bool add_enchantment(const EnchInfo& info);
    bool remove_enchantment(const std::string& name_id);
    bool modify_enchantment(const std::string& name_id, const EnchInfo& patch);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const std::string& name_id);
    bool add_category(const std::string& name);

    // ── Registry access ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const EquipmentTagRegistry& categories() const noexcept;

    // ── Persistence ──
    bool export_registry(const std::string& path) const;

    // ── Solve ──
    SolveResult solve(const SolveInput& input);
    std::vector<std::string> list_algorithms() const;
};
```

### 7.2 SolvePipeline

Stateless pipeline that orchestrates the solve process. Broken into stages for testability:

```
apply()    → Build AlgorithmInput from SolveRequest + EnchReg
execute()  → Run algorithm (resolve items + search)
recall()   → Convert AlgorithmOutput back to domain Solution[]
```

---

## 8. Cross-Domain Relations

### Dependencies

```
interface/
├── protocol/       → business/types/ (Item, EnchSet, Solution, Equipment)
│                   → algorithm/types/ (ForgeConfig, SearchConfig)
│
├── api/            → business/ (Profile, ProfileManager, registries)
│                   → orchestration/ (CompactAdapter)
│                   → algorithm/ (AlgorithmLoader)
│
├── cli/            → business/ (EnchInfo, Equipment, registries)
│                   → protocol/ (SolveRequest) — via cli.h helpers
│                   → types/ (SpecTypes)
│
└── abi/            → api/ (BesqContext) — via include/besq/besq.h
```

### What Interface Does NOT Own

| Concern | Owner |
|---------|-------|
| Game data types | `business/types/` |
| Data file parsing | `business/parsers/` |
| Format detection | `business/components/FormatDetector` |
| Profile management | `business/managers/ProfileManager` |
| Data loading | `business/loaders/` |
| Registry serialization | `business/components/Serializer` |
| Algorithm execution | `algorithm/` + `orchestration/` |
| Compact type adapters | `orchestration/components/` |

---

## 9. Implementation Plan

### Phase 1: Foundation (S1–S2) — Current

| Step | Files | Description |
|------|-------|-------------|
| **S1** | `protocol/SolveRequest.h`, `protocol/SolveResponse.h` | Extract protocol types from SolvePipeline.h |
| **S2** | `cli/CLIParser.h/cpp`, `cli/EnchParser.h/cpp`, `cli/ItemParser.h/cpp` | Move CLI parsers under `cli/` module |

### Phase 2: Reorganize (S3–S4)

| Step | Files | Description |
|------|-------|-------------|
| **S3** | `components/ParserUtilsDomain.hpp`, `fs/FileFormat.h` | Remove remnants absorbed by business domain |
| **S4** | `interface.h`, `CMakeLists.txt` | Update umbrella header and build files |

### Phase 3: Future Modules

| Step | Module | Description |
|------|--------|-------------|
| **S5** | TUI (P2) | Terminal UI extension |
| **S6** | GUI (P2) | Desktop GUI extension |
| **S7** | Web API (P3) | HTTP/WS extension |
