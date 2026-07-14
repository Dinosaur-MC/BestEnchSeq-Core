# BestEnchSeq-Core

## Overview

A C++20 tool (CLI: `besq`) to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It not only supports enchantments in Vanilla Minecraft, but also those in various mods, as it uses extensible enchantment sheets and a data-driven registry system to maintain enchantment information and can easily add third-party/custom enchantments.

### Key Features

- [x] Calculate the best enchanting order/forging sequence by your given needs
- [ ] Support inventory management, providing well handling of complex enchanted items/situations (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [x] Support third-party/custom enchantments by editing custom enchantment sheet
- [x] Support third-party/custom equipments by editing custom equipment sheet
- [x] Pluggable algorithm strategies: greedy, dfs, astar, penalty_balance, hierarchical, idastar, hamming
- [x] Asynchronous execution with pause/resume/cancel and streaming progress
- [x] Moddable forge engine via IForgeEngine virtual interface
- [x] Input validation + EnchReg pruning via CompactAdapter::apply()
- [x] Easily export to share calculation results with others
- [ ] Optionally hosting a RESTful API service for external applications

### Quick Start

#### From source code

```bash
cmake -S . -B build
cmake --build build
besq --target diamond_sword --wanted "sharpness=5,knockback=2"
besq --algorithm astar --target diamond_sword --wanted "sharpness=5,looting=3,unbreaking=3"
besq --algorithm penalty_balance --target diamond_chestplate --wanted "protection=4,thorns=3,unbreaking=3,mending=1"
besq --algorithm hamming --target netherite_sword --wanted "sharpness=5,sweeping_edge=3,looting=3,unbreaking=3,fire_aspect=2,knockback=2,mending=1,vanishing_curse=1"
```

Alternatively, invoke directly from the build directory:

```bash
./build/bin/besq --target diamond_sword --wanted "sharpness=5,knockback=2"
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

The project uses a **two-tier type system**: compact types (`namespace compact`) for algorithm hot paths, and domain types for I/O boundaries. The algorithm layer has zero domain type dependencies.

```
CLI → InputParser (domain)
  → CompactAdapter::apply() (validates + prunes EnchReg + converts)
  → AlgorithmInput (compact) → AlgorithmExecutor → IAlgorithm
  → AlgorithmOutput (compact steps)
  → CompactAdapter::recall() (restores IDs + builds solutions)
  → OutputFormatter (domain)
```

### Source Layout

```
src/
├── main.cpp                     ← Entrypoint: init registries → parse → CompactAdapter → executor → output
├── config/                      ← Configuration layer (leaf, only STL + EnvUtil)
│   ├── ForgeConfig.h            ← Forge behavior config (uses MCE from types/Platform.h)
│   ├── SearchConfig.h           ← Search limits (max_solutions, max_depth, memory_mb, time)
│   └── AppConfig.h              ← Application-level config (env-var backed)
├── adapters/                    ← Domain ↔ compact / serialization boundary
│   ├── CompactAdapter.h/.cpp    ← apply / recall (domain ↔ compact)
│   ├── RegistryResolver.h/.cpp  ← String → ID resolution
│   ├── OutputFormatter.h/.cpp   ← EnchSolution → text/compact/json
│   ├── EnchSerializer.h/.cpp    ← EnchInfo/Equipment ↔ JSON/CSV/MC official
│   └── Serializer.hpp           ← Binary serialization (uses io/ByteStream)
├── cli.h/.cpp                   ← CLIConfig + EnchantmentSpec + CLI parsers
├── types/                       ← Domain data types (pure data, no computation)
│   ├── CompactedTypes.h/.cpp    ← Compact types (namespace compact): Ench, EnchSet, Item, EnchStep
│   ├── EnchInfo.h/.cpp          ← Full enchantment definition
│   ├── EnchSet.h/.cpp           ← EnchSet container
│   ├── Ench.h                   ← Enchantment ID + level pair
│   ├── ItemStack.h/.cpp         ← Item with enchantments
│   ├── Equipment.h/.cpp         ← Equipment definition
│   ├── EnchSolution.h/.cpp      ← Solution type
│   ├── Platform.h               ← MCE enum (Minecraft edition Java/Bedrock/All)
│   ├── CLITypes.h               ← CLIConfig, EnchantmentSpec, TargetSpec
│   ├── AlgorithmTypes.h         ← AlgorithmInput, AlgorithmOutput, ProgressStatus, AlgorithmState
│   ├── LogTypes.h               ← LogLevel, LogEntry
│   ├── RawTypes.h               ← String-based intermediates (RawEnchInfo, RawEquipment)
│   └── EquipmentCategory.h      ← Equipment category constants
├── log/                         ← Global async Logger (singleton; uses LogLevel/types)
│   ├── Logger.hpp/.cpp          ← Async logger (atomic::wait, zero-CPU idle)
│   └── log.hpp                  ← Free-function wrappers + LOG_INFO / LOG_WARN macros
├── registries/                  ← Domain registries + tag resolution
│   ├── EnchantmentRegistry.h/.cpp  ← Full enchantment registry with subset derivation
│   ├── EquipmentRegistry.h/.cpp
│   ├── EquipmentCategoryRegistry.h/.cpp
│   ├── AlgorithmRegistry.h/.cpp ← Algorithm factory
│   ├── CompactedRegistries.h/.cpp ← EnchReg: compact subset with O(1) conflict matrix
│   ├── RegistryAccess.h         ← Meyer's singleton accessors
│   └── TagResolver.hpp          ← Tag reference (#tag) resolution (from utils/)
├── parsers/                     ← Input parsing (zero registry dependencies)
│   ├── CLIParser.h/.cpp         ← Generic key-value CLI parser
│   ├── InputParser.h/.cpp       ← CLIConfig → ParsedInput
│   ├── EnchInfoParser.h/.cpp    ← Data file → RawEnchInfo
│   ├── EquipmentParser.h/.cpp   ← Data file → RawEquipment
│   └── ParserUtilsDomain.hpp    ← Domain helpers (DataFormat, parse_platform, split_namespace; from utils/)
├── algorithm/                   ← Zero domain types
│   ├── IAlgorithm.h             ← AlgorithmInput + AlgorithmOutput + IAlgorithm interface
│   ├── AlgorithmExecutor.h/.cpp ← Async engine (thread lifecycle + observer dispatch)
│   ├── ExecutionContext.h/.cpp  ← Cancel/pause/progress + accumulator
│   ├── AlgorithmObserver.h      ← Streaming callbacks
│   ├── DiagnosticsWriter.h/.cpp ← Diagnostics persist-to-disk (separated from data structs)
│   ├── Utils.h                  ← Shared helpers (meets_target())
│   ├── components/              ← Algorithm building blocks
│   │   ├── Heuristic.h          ← Pool-based admissible heuristic
│   │   ├── HeuristicBasic.h     ← Direct Item-vector heuristic (no ItemPool dep)
│   │   ├── StateHash.h          ← State hashing utilities
│   │   ├── TTTable.h            ← Epoch-based IDA* transposition table
│   │   ├── ItemPool.h           ← Hash-dedup item pool (backed by MemoryPool)
│   │   ├── AlgorithmDiagnostics.h  ← Pure-data diagnostics struct
│   │   ├── AStarDiagnostics.h   ← Pure-data AStar diagnostics struct
│   │   └── AStarMemoryBudget.h/.cpp ← AStar memory budget estimator
│   ├── forge/
│   │   ├── IForgeEngine.h       ← Virtual interface + default sub-ops
│   │   └── ForgeEngine.h/.cpp   ← Vanilla implementation
│   └── strategies/
│       ├── GreedyAlgorithm.*    ← Fast approximate
│       ├── HammingAlgorithm.*   ← Popcount-balanced merge tree (near-optimal, 0ms)
│       ├── DFSAlgorithm.*       ← Heuristic search (branch-and-bound, approx)
│       ├── AStarAlgorithm.*     ← Exact optimal (admissible heuristic)
│       ├── DynamicPenaltyBalancingAlgorithm.* ← High-quality approx
│       ├── HierarchicalMergeAlgorithm.* ← Large-scale approx
│       └── IDAStarAlgorithm.*   ← DFS + TTTable (memory-efficient exact)
├── utils/                       ← Generic utilities, zero project dependencies
│   ├── ParserUtils.hpp          ← String/JSON/file helpers (general)
│   ├── EnvUtil.hpp              ← Env-var access (get_env<T>)
│   ├── ExpCalculator.hpp        ← Level ↔ XP conversion
│   ├── HashUtils.hpp            ← hash_combine()
│   ├── FlatHashMap.hpp          ← Open-addressing hash map
│   ├── MemoryPool.hpp           ← PMR monotonic buffer memory resource
│   ├── ObjectPool.hpp           ← Fixed-size freelist object pool
│   ├── EventLoop.hpp            ← atomic::wait event loop
│   └── queue/                   ← Lock-free MPMC queue family
│       ├── IQueue.h             ← Virtual queue interface + QueueType concept
│       ├── BoundedMPMCQueue.hpp ← MPMC bounded (Vyukov)
│       ├── SegmentedMPMCQueue.hpp ← MPMC unbounded
│       ├── SPSCQueue.hpp        ← SPSC bounded
│       └── SPMCQueue.hpp        ← SPMC bounded
├── io/                          ← I/O primitives (leaf, zero project deps)
│   ├── json.h/.cpp              ← Generic JSON DOM (parse + serialize)
│   ├── CsvIO.h/.cpp             ← CSV parse / format / write
│   └── ByteStream.h             ← Binary byte I/O (u8/16/32/64, varint, string)
├── data/
│   └── builtin/vanilla.json     ← Built-in Vanilla Minecraft enchantment data
└── BESQTypes.h                  ← Umbrella include for domain types
```

See `docs/MPMCQueue.md` for the full design documentation.

### Key Design Decisions

**Compact types**: `Ench` (4 bytes), `compact::EnchSet` (sorted vector, O(log N) lookup), `Item` (type + dur + ppn + enchs). No pointer indirection, no virtual dispatch, zero domain dependencies.

**Flat conflict matrix**: `EnchReg` stores an N×N `vector<char>` for O(1) incompatibility checks — single allocation, contiguous memory.

**EnchReg pruning**: `CompactAdapter::apply()` builds a subset of the global registry (via `EnchantmentRegistry::create_subset()`) that only includes enchantments applicable to the target equipment. Smaller conflict matrix, faster lookups.

**IForgeEngine virtual interface**: All forge sub-operations (`penalty_cost`, `apply_cap`, `estimate_forge_cost`) have default vanilla implementations. Subclass only what you need for modded rules. `ForgeEngine` overrides to respect `ForgeConfig` flags. Book multiplier is precomputed as `compact::EnchInfo::mul_b` at data-load time.

**AlgorithmInput owns data**: `EnchReg`, `Equipment`, and item collections are stored by value — no pointers, no external lifetime dependencies. Once passed to `executor.start()`, ownership transfers completely.

**Domain types are pure data**: `EnchSet`, `Ench`, `ItemStack` are containers only. All combine/cost/penalty computation moved to compact forge engine.

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
