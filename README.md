# BestEnchSeq-Core

## Overview

A C++20 tool (CLI: `besq`) to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It not only supports enchantments in Vanilla Minecraft, but also those in various mods, as it uses extensible enchantment sheets and a data-driven registry system to maintain enchantment information and can easily add third-party/custom enchantments.

### Key Features

- [x] Calculate the best enchanting order/forging sequence by your given needs
- [x] Support inventory management, providing well handling of complex enchanted items/situations (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [x] Support third-party/custom enchantments by editing custom enchantment sheet
- [x] Support third-party/custom equipments by editing custom equipment sheet
- [x] Pluggable algorithm strategies: greedy, dfs, astar, penalty_balance, hierarchical, idastar, hamming, diff_first
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
CLI → Interface CLI parsers (CLIParser, EnchParser, ItemParser)
  → BesqContext (session facade)
  → Orchestration Pipeline (SolvePipeline / ManagePipeline / ExportPipeline)
  → CompactAdapter::apply() (validates + prunes EnchReg + converts)
  → AlgorithmInput (compact) → AlgorithmExecutor → IAlgorithm
  → AlgorithmOutput (compact steps)
  → CompactAdapter::recall() (restores IDs + builds solutions)
  → OutputFormatter (domain)
```

### Domain Overview

| Domain | Namespace | Purpose | External Dependencies |
|--------|-----------|---------|----------------------|
| `algorithm/` | `algorithm::` | Algorithm execution: compact types, forge engine, search strategies, diagnostics | `common/` only |
| `business/` | `::` | Business types, registries, Profile, parsers, loaders, managers | `common/` only |
| `interface/` | `::` | I/O boundary: CLI parsing, C ABI, BesqContext | `common/`, `business/`, `orchestration/` |
| `orchestration/` | `orchestration::` | Cross-domain wiring: pipelines, CompactAdapter, formatters | All domains |

### Source Layout

```
src/
├── main.cpp                             ← Entry point
├── BESQTypes.h                          ← Umbrella include
├── builtin/                             ← Built-in data (embedded resource → DTO)
│   ├── DataLoader.h/.cpp                ← Load built-in data from embedded JSON
│   ├── EmbeddedData.h                   ← Embedded resource declarations
│   └── ItemProperties.h/.cpp            ← Vanilla item property definitions
├── common/                              ← Shared utilities (zero domain deps)
│   ├── CommonTypes.h/.cpp               ← NSID, MCE enum
│   ├── io/                              ← JSON DOM, CSV reader/writer, ByteStream
│   │   ├── json.h/.cpp                  ← Recursive-descent JSON parser
│   │   ├── CsvIO.h/.cpp                 ← CSV read/write
│   │   ├── ByteStream.h                 ← Binary stream
│   │   └── FileUtils.hpp                ← File I/O helpers
│   ├── log/                             ← Global async Logger
│   ├── serialization/                   ← Serialization interfaces
│   │   ├── ISerializable.h              ← Format-independent base
│   │   ├── IJsonSerializable.h          ← to_json() / from_json()
│   │   └── IBinarySerializable.h        ← serialize() / deserialize()
│   └── utils/                           ← Generic utilities
│       ├── EventLoop.hpp                ← Zero-CPU-idle event loop
│       ├── MemoryPool.hpp               ← Monotonic buffer
│       ├── ObjectPool.hpp               ← Freelist pool
│       ├── FlatHashMap.hpp              ← Open-addressing hash map
│       ├── HashUtils.hpp                ← hash_combine()
│       ├── StringUtils.hpp              ← String helpers
│       ├── EnvUtil.hpp                  ← Type-safe env-var access
│       ├── ExpCalculator.hpp            ← Level ↔ XP conversion
│       └── queue/                       ← Lock-free queue family
├── domain/
│   ├── algorithm/                       ← Algorithm domain (zero business deps)
│   │   ├── IAlgorithm.h/.cpp            ← AlgorithmInput/Output + IAlgorithm
│   │   ├── AlgorithmExecutor.h/.cpp     ← Async engine
│   │   ├── ExecutionContext.h/.cpp       ← Control + progress + solution accumulator
│   │   ├── types/                       ← Compact types (Enchantment, EnchSet, Item, etc.)
│   │   ├── registries/                  ← EnchReg + AlgorithmRegistry
│   │   ├── forge_engine/                ← IForgeEngine + ForgeEngine
│   │   ├── _strategies/                 ← Built-in strategies (astar, dfs, hamming)
│   │   ├── components/                  ← Heuristic, ItemPool, StateHash, SearchUtils
│   │   ├── diagnostics/                 ← Event-driven diagnostics pipeline
│   │   ├── serialization/               ← Binary checkpoint
│   │   ├── plugin/                      ← AlgorithmLoader
│   │   └── resolvers/                   ← ItemResolver, InventoryResolver
│   ├── business/                        ← Business domain (self-contained)
│   │   ├── types/                       ← Domain types + DTOs
│   │   ├── registries/                  ← EnchantmentRegistry, EquipmentRegistry, etc.
│   │   ├── parsers/                     ← File format parsers (JSON/CSV/MC)
│   │   ├── loaders/                     ← RegistryLoader, ProfileLoader
│   │   ├── managers/                    ← RegistryManager, ProfileManager
│   │   └── components/                  ← FormatDetector, Serializer, TagResolver
│   ├── interface/                       ← Interface domain (I/O boundary)
│   │   ├── BesqContext.h/.cpp           ← Session facade
│   │   ├── cli/                         ← CLI parsing
│   │   └── abi/                         ← C ABI
│   └── orchestration/                   ← Orchestration domain (glue)
│       ├── types/                       ← Pipeline contracts (SolveRequest, etc.)
│       ├── pipelines/                   ← SolvePipeline, ManagePipeline, ExportPipeline
│       └── components/                  ← CompactAdapter, OutputFormatter, EnchSerializer
├── data/
│   ├── CMakeLists.txt
│   └── builtin/vanilla.json             ← Embedded built-in data

tests/                                   ← Standalone test executables
benchmarks/                              ← Performance benchmarks
```

### Algorithm Strategies

| Strategy | Type | Optimality | Scale | Origin | Mechanism |
|----------|------|-----------|-------|--------|-----------|
| Greedy | Approx | No | Any | Plugin (plugins/) | Cost-sorted greedy merge |
| Penalty Balance | Approx | No | Any | Plugin (plugins/) | Merge closest penalty pairs |
| Hierarchical | Approx | No | Large | Plugin (plugins/) | Hierarchical group-then-merge |
| DiffFirst | Approx | No | Any | Plugin (plugins/) | PPN-layer, cheapest pair per layer |
| Hamming | Approx | No | Large | Built-in (src/) | Popcount-balanced binary merge tree |
| DFS | Exact | Yes | ≤ 8 | Built-in (src/) | B&B + hash memoization |
| A* | Exact | Yes | ≤ 9 | Built-in (src/) | Admissible heuristic + priority queue |
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

**Four-domain layering**: `algorithm/` depends on nothing outside `common/`. `business/` adds domain types and registries. `interface/` adds CLI, C ABI, BesqContext. `orchestration/` wires everything via pipelines.

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
