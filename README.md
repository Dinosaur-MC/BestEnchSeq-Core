# BestEnchSeq-Core

## Overview

A C++20 tool (CLI: `besq`) to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It not only supports enchantments in Vanilla Minecraft, but also those in various mods, as it uses extensible enchantment sheets and a data-driven registry system to maintain enchantment information and can easily add third-party/custom enchantments.

### Key Features

- [x] Calculate the best enchanting order/forging sequence by your given needs
- [x] Support inventory management, providing well handling of complex enchanted items/situations (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [x] Support third-party/custom enchantments by editing custom enchantment sheet
- [x] Support third-party/custom equipments by editing custom equipment sheet
- [x] Pluggable algorithm strategies: hamming, dfs, astar, dp_merge, plus external plugins
- [x] Asynchronous execution with pause/resume/cancel and streaming progress
- [x] Moddable forge engine via IForgeEngine virtual interface
- [x] Profile-based data management with versioning, branching, merging
- [x] Multi-format data loading: vanilla JSON, CSV, MC data-driven format
- [x] Built-in + plugin algorithm strategies
- [x] Binary checkpointing for long-running searches
- [x] Input validation + EnchReg pruning via CompactAdapter::apply()
- [ ] Optionally hosting a RESTful API service for external applications

### Quick Start

**Requirements:** C++20 toolchain (Clang 15+), CMake 3.20+, Ninja.
The project uses C++20 features unconditionally — concepts, `if constexpr`,
`std::jthread`, atomic `wait`/`notify`, etc. No feature-test macros or
fallbacks. C++17 or earlier is not supported.

#### From source code

```bash
cmake -S . -B build
cmake --build build
besq --target diamond_sword --source "sharpness=5"
besq --algorithm astar --target "diamond_sword[sharpness=5,looting=3,unbreaking=3]"
besq --algorithm penalty_balance --target "diamond_chestplate[protection=4,thorns=3,unbreaking=3,mending=1]"
besq --algorithm hamming --target "netherite_sword[sharpness=5,sweeping_edge=3,looting=3,unbreaking=3,fire_aspect=2,knockback=2,mending=1,vanishing_curse=1]"
```

Alternatively, invoke directly from the build directory:

```bash
./build/bin/besq --target diamond_sword --source "sharpness=5"
```

### Running tests

```bash
cd build && ctest --output-on-failure
```

### Benchmark

```bash
./build/bin/forge_benchmark --group sword --algo greedy,dfs
./build/bin/forge_benchmark --list
./build/bin/forge_benchmark --help
```

## Architecture

The project uses a **four-domain architecture** built on shared utilities.

```
CLI → CLIApp (application runner)
  → BesqContext (session facade, pImpl)
  → Orchestration Pipeline (SolvePipeline / ManagePipeline / ExportPipeline)
  → CompactAdapter::apply() (validates + prunes EnchReg + converts)
  → AlgorithmInput (compact) → AlgorithmExecutor → IAlgorithm
  → AlgorithmOutput (compact steps)
  → CompactAdapter::recall() (restores IDs + builds solutions)
  → OutputFormatter (domain)
```

### Domain Overview

| Domain | Namespace | Purpose | Dependencies |
|--------|-----------|---------|-------------|
| `algorithm/` | `algorithm::` | Algorithm execution: compact types, forge engine, strategies, diagnostics | `common-core` + log |
| `business/` | `::` | Business types, registries, Profile, parsers, loaders, managers | `common-core` + io + log |
| `orchestration/` | `orchestration::` | Cross-domain wiring: pipelines, CompactAdapter, formatters | algorithm + business |
| `interface/` | `::` | I/O boundary: CLIApp, BesqContext, C ABI | orchestration + common sub-targets |

### Source Layout

```
src/
├── main.cpp                             ← Entry point (router)
├── AppConfig.h                          ← Environment config loader
├── BuildConfig.h.in                     ← Generated config (version, project name)
├── builtin/                             ← Built-in data (embedded resource → DTO)
│   ├── DataLoader.h/.cpp                ← Load built-in data from embedded JSON
│   ├── I18nLoader.h/.cpp                ← Register built-in translations
│   └── ItemProperties.h/.cpp            ← Vanilla item property definitions
├── common/                              ← Shared utilities (5 independent sub-libraries)
│   ├── CommonTypes.h/.cpp               ← NSID, MCE enum, AlgorithmMode
│   ├── io/                              ← JSON DOM, CSV reader/writer, ByteStream
│   ├── log/                             ← Global async Logger + log.hpp wrappers
│   ├── i18n/                            ← Language manager, LocaleDetector
│   ├── serialization/                   ← Serialization interfaces
│   └── utils/
│       ├── cli/                         ← C++20 CLIParser v2
│       ├── queue/                       ← Lock-free queue family
│       └── ...                           ← MemoryPool, EventLoop, HashUtils, etc.
├── domain/
│   ├── algorithm/                       ← Algorithm domain (compact types)
│   │   ├── _strategies/                 ← Built-in strategies (astar, dfs, dp_merge, hamming)
│   │   ├── plugin/                      ← AlgorithmLoader (hot-load external .so/.dll)
│   │   ├── forge_engine/                ← IForgeEngine + ForgeEngine
│   │   ├── diagnostics/                 ← Event-driven diagnostics pipeline
│   │   ├── serialization/               ← Binary checkpoint
│   │   └── ...
│   ├── business/                        ← Business domain (domain model)
│   │   ├── types/                       ← Domain types + DTOs
│   │   ├── registries/                  ← EnchantmentRegistry, EquipmentRegistry, etc.
│   │   ├── parsers/                     ← File format parsers (JSON/CSV/MC)
│   │   ├── loaders/                     ← RegistryLoader, ProfileLoader
│   │   ├── managers/                    ← RegistryManager, ProfileManager
│   │   └── components/                  ← FormatDetector, Serializer, TagResolver
│   ├── orchestration/                   ← Cross-domain glue
│   │   ├── types/                       ← Pipeline contracts (SolveRequest, SolveResult)
│   │   ├── pipelines/                   ← SolvePipeline, ManagePipeline, ExportPipeline
│   │   └── components/                  ← CompactAdapter, OutputFormatter, EnchSerializer
│   └── interface/                       ← I/O boundary
│       ├── BesqContext.h/.cpp           ← Session facade (pImpl)
│       ├── cli/                         ← CLIApp, EnchParser, ItemParser, RegistryEditor
│       └── abi/                         ← C ABI
├── data/
│   ├── builtin/vanilla.json             ← Embedded built-in data
│   ├── builtin/item_properties.json
│   └── i18n/                            ← Translation tables (zh_CN, en_US)
└── include/
    └── besq/besq.h                       ← Public umbrella header

tests/                                   ← Standalone test executables
benchmarks/                              ← Performance benchmarks
```

### Algorithm Strategies

| Strategy | Type | Optimality | Scale | Origin | Mechanism |
|----------|------|-----------|-------|--------|-----------|
| DP Merge | Approx | No | Large | Built-in (src/) | Dynamic programming merge (default) |
| Hamming | Approx | No | Large | Built-in (src/) | Popcount-balanced binary merge tree |
| DFS | Exact | Yes | ≤ 8 | Built-in (src/) | B&B + hash memoization |
| A* | Exact | Yes | ≤ 9 | Built-in (src/) | Admissible heuristic + priority queue |
| Greedy | Approx | No | Any | Plugin (plugins/) | Cost-sorted greedy merge |
| Penalty Balance | Approx | No | Any | Plugin (plugins/) | Merge closest penalty pairs |
| Hierarchical | Approx | No | Large | Plugin (plugins/) | Hierarchical group-then-merge |
| DiffFirst | Approx | No | Any | Plugin (plugins/) | PPN-layer, cheapest pair per layer |
| IDA* | Exact | Yes | ≤ 10 | Plugin (plugins/) | Iterative deepening + TT pruning |

All algorithms share `IForgeEngine` and compact types. New algorithms only need to implement `IAlgorithm::execute()` to gain thread management, pause/cancel, and progress reporting.

### Key Design Decisions

**Compact types (`algorithm::`)**: `algorithm::Enchantment` (4 bytes: int16_t id + level), `algorithm::EnchSet` (sorted vector, O(log N) lookup), `algorithm::Item` (type + dur + ppn + enchs). No pointer indirection, no virtual dispatch, zero domain dependencies.

**Flat conflict matrix**: `algorithm::EnchReg` stores an N×N `vector<char>` for O(1) incompatibility checks — single allocation, contiguous memory.

**EnchReg pruning**: `CompactAdapter::apply()` builds a subset of the global registry that only includes enchantments applicable to the target equipment. Smaller conflict matrix, faster lookups.

**IForgeEngine virtual interface**: All forge sub-operations have default vanilla implementations. Subclass only what you need for modded rules. `ForgeEngine` overrides to respect `ForgeConfig` flags.

**AlgorithmInput owns data**: `algorithm::EnchReg`, `algorithm::Item` vector, and target collection are stored by value — no pointers, no external lifetime dependencies. The struct owns two config sub-objects: `f_config` (forge config, `ForgeConfig`) and `s_config` (search config, `SearchConfig`).

**Profile as first-class citizen**: All pipelines receive `Profile` (or `ProfileManager`), never raw registries extracted from Profile. `Profile` owns `EnchantmentRegistry`, `EquipmentRegistry`, and `EquipmentTagRegistry` as a unit.

**Pipeline pattern**: Every pipeline is a standalone struct with a single `run()` method. No polymorphism, no registration — dispatch by switch at `BesqContext` or `main.cpp`.

**Serialization interfaces**: Business types implement `IJsonSerializable` (`to_json()` / `from_json()`). ADL-compatible free functions in `Serializer.h` delegate to member implementations.

**Four-domain layering**: `algorithm/` depends only on `besq-common-core` + log. `business/` adds domain types and registries (depends on core + io + log). `orchestration/` wires algorithm + business together. `interface/` adds CLIApp, BesqContext, C ABI (depends on orchestration + common sub-targets). `besq-common/` is split into 5 independent static libraries (core, io, log, i18n, cli) — targets link only what they need.

**No global platform singleton**: Platform (`MCE::Java` / `MCE::Bedrock`) flows through `ForgeConfig` → `AlgorithmInput` → `IForgeEngine`. No global mutable state.

## Scripts

| Script | Purpose |
|--------|---------|
| `scripts/evaluate.sh` | WSL-based Valgrind evaluation — leak check, Callgrind/Massif profiling, benchmark |
| `scripts/get_vanilla_data.py` | Extract enchantment/equipment data from official Minecraft client jar → `data/builtin/vanilla.json` |
| `scripts/parse_callgrind.py` | Parse Callgrind `callgrind.out` → function hotspot ranking |
| `scripts/parse_massif.py` | Parse Massif `ms_print` output → heap memory change chart + allocation hotspots |

## License

> MIT License
