# Orchestration Domain Design

> Version: 1.0
> Last updated: 2026-07-26

---

## Table of Contents

1. [Overview](#1-overview)
2. [Core Principles](#2-core-principles)
3. [Directory Structure](#3-directory-structure)
4. [Types](#4-types)
5. [Pipelines](#5-pipelines)
6. [Components](#6-components)
7. [Cross-Domain Relations](#7-cross-domain-relations)
8. [Data Flow](#8-data-flow)
9. [Migration Plan](#9-migration-plan)

---

## 1. Overview

The orchestration domain (`src/domain/orchestration/`) is the **cross-domain glue layer** of BestEnchSeq. It owns the solve pipeline, profile management routing, and data export — all coordination that involves multiple domains.

### What Orchestration Is

- **Pipeline coordinator** — every major task (solve, manage, export) has a dedicated pipeline that calls into domain components in the right order
- **Profile-aware** — all pipelines receive `Profile` as the primary authorization unit; raw registry access is an internal detail
- **Interface-agnostic** — orchestration does not depend on the interface domain; its core pipelines can run directly from `main.cpp`

### What Orchestration Is Not

- Not a business logic container — business rules stay in `business/`
- Not an algorithm extension — search strategies stay in `algorithm/`
- Not an I/O boundary — input parsing and output formatting for external consumers belong to `interface/`

### Dependency Direction

```
orchestration/ → algorithm/   (executor, loader, compact types)
              → business/     (Profile, registries, managers)
              → common/       (utilities, JSON, logging)
              → interface/    ❌ (forbidden)
```

---

## 2. Core Principles

### 2.1 Pipeline Pattern

Every pipeline is a **standalone struct with a single `run()` method**. Pipelines are not polymorphic — they are concrete classes with concrete input/output types. The entry point (`main.cpp`, `BesqContext`) dispatches by task type via direct call or switch.

```cpp
// Every pipeline follows this shape:
struct XxxPipeline {
    static XxxResult run(
        /* domain dependencies */,
        const XxxRequest& request
    );
};
```

New pipelines can be added by creating a new file in `pipelines/` and adding a dispatch branch in the entry point. No registration mechanism, no type erasure, no virtual dispatch.

### 2.2 Profile as First-Class Citizen

All pipelines receive `Profile` (or `ProfileManager`), never raw registries extracted from Profile. CompactAdapter and collaborators are Profile-aware:

```cpp
// Before (business refactoring missed orchestration):
CompactAdapter::apply(target, source, eq, EnchantmentRegistry)

// After (Profile-aware):
CompactAdapter::apply(const Profile& profile, const SolveRequest& request)
```

### 2.3 Type-Safe Requests

Request types use enums and variants instead of strings and booleans:

| Anti-pattern | Correct |
|---|---|
| `bool is_inventory_mode` | `AlgorithmMode mode` |
| `std::string format` | `ExportRequest::Format` enum |
| `std::string mode_name` | `AlgorithmMode mode` |
| optional fields for different modes | `std::variant<DirectPayload, InventoryPayload>` |

### 2.4 Separation of Formatting

`SolveResult` is a pure data container — no `to_json()` or `to_text()` methods. All formatting goes through `OutputFormatter` (or `ExportPipeline`). This keeps the type lightweight, testable, and independent of registry dependencies.

---

## 3. Directory Structure

```
src/domain/orchestration/
├── orchestration.h                     ← Umbrella header
├── CMakeLists.txt
│
├── types/                              ← Pipeline contracts (pure data)
│   ├── SolveRequest.h                  Solve request + DirectPayload / InventoryPayload
│   ├── SolveResult.h                   Solve output
│   ├── ManageRequest.h                 Manage operations
│   ├── ManageResult.h                  Manage output
│   ├── ExportRequest.h                 Export specification
│   └── ExportResult.h                  Export output
│
├── pipelines/                          ← Task coordinators
│   ├── SolvePipeline.h/.cpp            Forge solve pipeline
│   ├── ManagePipeline.h/.cpp           Profile/registry management pipeline
│   └── ExportPipeline.h/.cpp           Data export pipeline
│
└── components/                         ← Shared adapters and formatters
    ├── CompactAdapter.h/.cpp           Business ↔ compact type conversion
    ├── OutputFormatter.h/.cpp          Solution formatting (text, JSON, compact)
    └── EnchSerializer.h/.cpp           Registry serialization (JSON, CSV, MC official)
```

---

## 4. Types

### 4.1 SolveRequest / SolveResult

```cpp
// types/SolveRequest.h
#pragma once
#include "domain/business/types/Item.h"
#include "domain/business/types/EnchSet.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "common/CommonTypes.h"
#include <string>
#include <variant>
#include <vector>

// ── Mode-specific payloads ──

struct DirectPayload {
    EnchSet source_enchantments;
};

struct InventoryPayload {
    std::vector<Item> extra_items;
    std::vector<int32_t> extra_item_priorities;
};

using SolvePayload = std::variant<DirectPayload, InventoryPayload>;

// ── Solve request ──

struct SolveRequest {
    Item target_item;
    AlgorithmMode mode;
    SolvePayload payload;
    algorithm::ForgeConfig forge_config;
    algorithm::SearchConfig search_config;
    std::string algorithm = "hamming";
};
```

```cpp
// types/SolveResult.h
#pragma once
#include "domain/business/types/Solution.h"
#include <string>
#include <vector>

struct SolveResult {
    bool success = false;
    std::vector<Solution> solutions;
    std::string algorithm_used;
    int64_t computation_time_ms = 0;
};
```

### 4.2 ManageRequest / ManageResult

```cpp
// types/ManageRequest.h
#pragma once
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "common/CommonTypes.h"
#include <string>
#include <vector>

struct ManageRequest {
    enum class Action {
        // Profile lifecycle
        LoadBuiltin,
        LoadFile,
        LoadData,
        CreateProfile,
        ActivateProfile,
        ForkProfile,
        MergeProfile,
        RemoveProfile,
        ListProfiles,
        // Registry editing
        AddEnchantment,
        RemoveEnchantment,
        ModifyEnchantment,
        AddEquipment,
        RemoveEquipment,
        AddCategory,
    };

    Action action;

    // Per-action parameters (only relevant ones used per action)
    std::string file_path;
    std::vector<std::string> filters;       // LoadData
    NSID profile_name;
    NSID source_name;
    NSID dest_name;
    EnchInfo ench_info;
    Equipment equip;
    std::string category_name;
};
```

```cpp
// types/ManageResult.h
#pragma once
#include <string>
#include <vector>

struct ManageResult {
    bool success = true;
    std::string message;
    std::vector<std::string> profile_list;  // ListProfiles
};
```

### 4.3 ExportRequest / ExportResult

```cpp
// types/ExportRequest.h
#pragma once
#include "domain/business/types/Solution.h"
#include "common/CommonTypes.h"
#include <string>
#include <vector>

struct ExportRequest {
    enum class Format {
        Json, Verbose, Compact,     // Solution output
        Csv, McOfficial,            // Registry export
    };
    enum class TargetType { Registry, Solution };

    TargetType target;
    Format format = Format::Json;
    std::string output_path;            // empty → return in content

    // Solution export
    std::vector<Solution> solutions;
    AlgorithmMode mode = AlgorithmMode::direct;
};
```

```cpp
// types/ExportResult.h
#pragma once
#include <string>

struct ExportResult {
    bool success = false;
    std::string output_path;
    std::string content;                // filled when output_path.empty()
};
```

---

## 5. Pipelines

### 5.1 SolvePipeline — Forge Solve

```cpp
// pipelines/SolvePipeline.h
#pragma once

struct SolvePipeline {
    static SolveResult run(
        Profile& profile,
        const SolveRequest& request,
        algorithm::AlgorithmLoader& loader
    );

    // Exposed for targeted testing; normal callers use run().
    struct Stage1Result;
    struct Stage2Result;

    static Stage1Result stage_apply(
        const Profile& profile,
        const SolveRequest& request
    );
    static Stage2Result stage_execute(
        algorithm::AlgorithmInput& algo_input,
        const std::string& algorithm,
        const algorithm::AlgorithmLoader& loader
    );
    static SolveResult stage_recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& algo_input
    );
};
```

**Internal stages**:

```
run()
  │
  ├── stage_apply()
  │     CompactAdapter::apply(profile, request) → AlgorithmInput
  │
  ├── stage_execute()
  │     loader.create(algorithm) → resolve() → simulate() → executor.start() → output
  │
  └── stage_recall()
        CompactAdapter::recall(output, input) → Solutions → SolveResult
```

**Key change from current `interface/SolvePipeline`**:
- `stage_apply()` replaces the current split between `SolvePipeline::apply()` (which did partial conversion) and `CompactAdapter::apply()` (which did the rest). Now one call does everything.
- The ad-hoc `to_algo_*` conversion helpers are eliminated — CompactAdapter owns all type mapping using its internal `global_to_local` table.

### 5.2 ManagePipeline — Profile and Registry Management

```cpp
// pipelines/ManagePipeline.h
#pragma once

struct ManagePipeline {
    static ManageResult run(
        ProfileManager& profiles,
        ProfileLoader& loader,
        const ManageRequest& request
    );
};
```

**Dispatch table**:

| Action | Implementation |
|--------|---------------|
| `LoadBuiltin` | `loader.load_builtin(profiles.create("builtin:vanilla"))` |
| `LoadFile` | `FormatDetector::parse` → `RegistryLoader` → merge into active profile |
| `LoadData` | Iterate filters, call LoadFile for each path |
| `CreateProfile` | `profiles.create(request.profile_name)` |
| `ActivateProfile` | `profiles.activate(request.profile_name)` |
| `ForkProfile` | `profiles.branch(request.source_name, request.dest_name)` |
| `MergeProfile` | `profiles.merge(request.source_name, request.dest_name)` |
| `RemoveProfile` | `profiles.remove(request.profile_name)` |
| `ListProfiles` | `profiles.list()` → populate `ManageResult.profile_list` |
| `AddEnchantment` | `profiles.active().add_enchantment(request.ench_info)` |
| `RemoveEnchantment` | `profiles.active().remove_enchantment(request.profile_name)` |
| `ModifyEnchantment` | Fetch → patch → `update_enchantment()` on active profile |
| `AddEquipment` | `profiles.active().add_equipment(request.equip)` |
| `RemoveEquipment` | `profiles.active().remove_equipment(request.profile_name)` |
| `AddCategory` | `profiles.active().add_tag({NSID, request.category_name})` |

Each action is a thin delegation to business domain. The pipeline's role is routing, not reimplementing.

### 5.3 ExportPipeline — Data Export

```cpp
// pipelines/ExportPipeline.h
#pragma once

struct ExportPipeline {
    static ExportResult run(
        const Profile& profile,
        const ExportRequest& request
    );
};
```

**Dispatch table**:

| Target | Format | Implementation |
|--------|--------|---------------|
| Registry | Json | `EnchSerializer::export_json(path, profile)` |
| Registry | Csv | `EnchSerializer::export_csv(path, profile)` |
| Registry | McOfficial | `EnchSerializer::export_to_mc_official(path, profile)` |
| Solution | Json | `OutputFormatter::format_json(solutions, profile, mode)` |
| Solution | Verbose | `OutputFormatter::format_verbose(solutions, profile, mode)` |
| Solution | Compact | `OutputFormatter::format_compact(solutions, profile, mode)` |

---

## 6. Components

### 6.1 CompactAdapter

Profile-aware type converter that bridges business domain types and algorithm compact types.

```cpp
struct CompactAdapter {
    /// Build AlgorithmInput from Profile + SolveRequest.
    /// Constructs EnchReg with correct NSID→local_id mapping,
    /// eliminating the previous `ench.id = 0` TEMP workaround.
    static algorithm::AlgorithmInput apply(
        const Profile& profile,
        const SolveRequest& request
    );

    /// Convert compact algorithm output back to domain Solution list.
    static std::vector<Solution> recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& input
    );

    /// Single-item conversion helpers.
    static algorithm::Item from_domain(
        const Item& item,
        const algorithm::EnchReg& reg
    );
    static Item to_domain(
        const algorithm::Item& item,
        const algorithm::EnchReg& reg
    );
};
```

**`apply()` internal flow**:

```
1. Resolve target equipment from Profile.eq()
     → algorithm::Equipment (category, max_durability)

2. Build compact registry from Profile.ench()
     → Sort by NSID for deterministic ordering
     → Assign local_id (int16_t) per applicable enchantment
     → Build conflict matrix (EnchReg)
     → Build global_to_local NSID→local_id map   ← fixes the TEMP hack

3. Convert target_item
     → algorithm::Item with correctly mapped enchantment IDs

4. Convert payload by mode
     DirectPayload   → algorithm::EnchCollection (remapped via global_to_local)
     InventoryPayload → algorithm::ItemCollection (each item remapped)

5. Assemble AlgorithmInput { ench_reg, target, mode, data, config }
```

**`recall()`** remains structurally similar to the current version:
- Iterate compact solutions
- Convert each step's `base`/`sacrifice` via `to_domain()`
- Calculate EXP cost from level cost
- Assemble domain `Solution` objects

### 6.2 OutputFormatter

Profile-aware solution formatter. No global mutable cache — all state is local to each call.

```cpp
struct OutputFormatter {
    static std::string format_verbose(
        const std::vector<Solution>& solutions,
        const Profile& profile,             // ← Profile replaces individual registries
        AlgorithmMode mode
    );

    static std::string format_compact(
        const std::vector<Solution>& solutions,
        const Profile& profile,
        AlgorithmMode mode
    );

    static std::string format_json(
        const std::vector<Solution>& solutions,
        const Profile& profile,
        AlgorithmMode mode
    );

    static std::vector<Solution> parse_json(
        const std::string& json_str,
        const Profile& profile              // ← Profile replaces individual registries
    );

    /// No more clear_cache() — no global state to clear.
};
```

**Key changes**:
- Receive `const Profile&` instead of `const EnchantmentRegistry&` + `const EquipmentTagRegistry&`
- Retrieve registries internally via `profile.ench()` / `profile.tags()`
- Remove `clear_cache()` and the anonymous-namespace `_json_eq_cache`
- JSON deserialization builds Equipment objects locally (owned by the parse call)

### 6.3 EnchSerializer

Profile-aware registry serializer. Maintains backward compatibility.

```cpp
struct EnchSerializer {
    // Enchantment serialization
    static std::string to_json(
        const std::vector<EnchInfo>& infos,
        const Profile& profile
    );
    static std::string to_csv(
        const std::vector<EnchInfo>& infos,
        const Profile& profile
    );
    static void export_to_mc_official(
        const std::vector<EnchInfo>& infos,
        const Profile& profile,
        const std::filesystem::path& output_dir
    );

    // Equipment serialization
    static std::string to_json(
        const std::vector<Equipment>& equipments,
        const Profile& profile
    );
    static std::string to_csv(
        const std::vector<Equipment>& equipments,
        const Profile& profile
    );

    // Full-registry export — takes Profile instead of three registries
    static bool export_json(
        const std::string& path,
        const Profile& profile
    );
    static bool export_csv(
        const std::string& path,
        const Profile& profile
    );
};
```

---

## 7. Cross-Domain Relations

### Dependency Graph

```
main.cpp
    │
    ├──→ interface/cli/       (CLI parsing only)
    ├──→ business/             (Profile, ProfileManager, managers)
    ├──→ algorithm/            (AlgorithmLoader)
    └──→ orchestration/        (the entry point below)
              │
              ├──→ pipelines/  (routes to the right pipeline)
              ├──→ components/ (CompactAdapter, formatters)
              │
              ├──→ business/   (Profile, registries, ProfileManager)
              ├──→ algorithm/  (compact types, executor, loader)
              └──→ common/     (JSON, logging, utilities)
```

### What Moves Where

| Current Location | New Location | Reason |
|-----------------|-------------|--------|
| `interface/SolvePipeline.h/.cpp` | `orchestration/pipelines/SolvePipeline.h/.cpp` | Pipeline belongs to orchestration |
| `interface/SolveInput` (inline in SolvePipeline.h) | `orchestration/types/SolveRequest.h` | Type contract |
| `interface/SolveResult` (inline in SolvePipeline.h) | `orchestration/types/SolveResult.h` | Type contract |
| `interface/ExecuteResult` (inline in SolvePipeline.h) | (dissolved into pipeline internals) | Internal detail |
| `interface/BesqContext.cpp` (solve/export routing) | delegates to orchestration pipelines | Interface calls orchestration |

### What Stays

| Component | Location | Notes |
|-----------|----------|-------|
| `CLI/cli.h/.cpp` | `interface/` | CLI argument parsing |
| `BesqContext` | `interface/` | Application facade, delegates to orchestration |
| `CompactAdapter` | `orchestration/components/` | Already in orchestration, just Profile-ified |
| `OutputFormatter` | `orchestration/components/` | Already in orchestration, just Profile-ified |
| `EnchSerializer` | `orchestration/components/` | Already in orchestration, just Profile-ified |

---

## 8. Data Flow

### Full Solve Flow

```
CLI args
  → interface/cli/parse_cli()         → CLIConfig
  → interface/cli/build_target()      → Item + EnchSet
  → Assemble SolveRequest
  → orchestration/SolvePipeline::run()
       │
       ├→ stage_apply()
       │   CompactAdapter::apply(profile, request)
       │     → profile.eq()      → find target → algorithm::Equipment
       │     → profile.ench()    → sort → assign local_ids → EnchReg
       │     → request.target    → remap → algorithm::Item
       │     → request.payload   → remap → SourceData
       │     → AlgorithmInput { ench_reg, target, mode, data }
       │
       ├→ stage_execute()
       │   → loader.create(algorithm)
       │   → algo.resolve(input)     → generate books / filter inventory
       │   → algo.simulate(input)    → reachability check
       │   → executor.start(input)   → search
       │   → AlgorithmOutput
       │
       └→ stage_recall()
           → CompactAdapter::recall(output, input)
           → SolveResult { solutions, algorithm_used, timing }
```

### Management Flow

```
External request (CLI / API)
  → Assemble ManageRequest { action, params }
  → ManagePipeline::run(profiles, loader, request)
       → dispatch switch
       → ProfileManager / ProfileLoader operation
       → ManageResult { success, message, profile_list }
```

### Export Flow

```
External request (CLI --export-registry / API)
  → Assemble ExportRequest { target, format, path, solutions }
  → ExportPipeline::run(profile, request)
       → dispatch switch
       → EnchSerializer / OutputFormatter
       → ExportResult { success, path, content }
```

---

## 9. Migration Plan

### Phase 1: Types + CompactAdapter

| Step | Files | Description |
|------|-------|-------------|
| 1.1 | `orchestration/types/*.h` | Create all 6 type headers |
| 1.2 | `components/CompactAdapter.h/.cpp` | Profile-ify `apply()`, fix `from_domain()` TEMP |
| 1.3 | `components/OutputFormatter.h/.cpp` | Profile-ify, remove global cache |
| 1.4 | `components/EnchSerializer.h/.cpp` | Add Profile-aware overloads |

### Phase 2: Pipelines

| Step | Files | Description |
|------|-------|-------------|
| 2.1 | `pipelines/SolvePipeline.h/.cpp` | Create with 3 stages, test |
| 2.2 | `pipelines/ManagePipeline.h/.cpp` | Create with action dispatch, test |
| 2.3 | `pipelines/ExportPipeline.h/.cpp` | Create with format dispatch, test |
| 2.4 | `orchestration.h`, `CMakeLists.txt` | Update umbrella + build |

### Phase 3: Cleanup

| Step | Files | Description |
|------|-------|-------------|
| 3.1 | `interface/SolvePipeline.h/.cpp` | Delete (moved to orchestration) |
| 3.2 | `interface/BesqContext.cpp` | Re-route solve/export through orchestration pipelines |
| 3.3 | `main.cpp` | Switch to Profile + orchestration |
| 3.4 | Update all includes | Fix headers across the project |
| 3.5 | Tests | Adapt to new types and signatures |

---

## References

- `src/domain/orchestration/` — Source directory
- `docs/project-design.md` — System architecture overview
- `docs/domain_designs/business-domain-design.md` — Business domain (Profile, managers)
