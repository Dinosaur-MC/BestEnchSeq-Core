# BestEnchSeq-Core

## Overview

A C++20 tool (CLI: `besq`) to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It not only supports enchantments in Vanilla Minecraft, but also those in various mods, as it uses extensible enchantment sheets and a data-driven registry system to maintain enchantment information and can easily add third-party/custom enchantments.

### Key Features

- [x] Calculate the best enchanting order/forging sequence by your given needs
- [ ] Support inventory management, providing well handling of complex enchanted items/situations (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [x] Support third-party/custom enchantments by editing custom enchantment sheet
- [x] Support third-party/custom equipments by editing custom equipment sheet
- [x] Pluggable algorithm strategies: greedy, dfs, astar, penalty_balance, hierarchical, idastar, hamming, diff_first
- [x] Asynchronous execution with pause/resume/cancel and streaming progress
- [x] Moddable forge engine via IForgeEngine virtual interface
- [x] Input validation + EnchReg pruning via CompactAdapter::apply()
- [x] Easily export to share calculation results with others
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

The project uses a **two-tier type system**: compact types (`namespace compact`) for algorithm hot paths, and domain types for I/O boundaries. The algorithm layer has zero domain type dependencies.

```
CLI → CLIParser + ItemParser/EnchParser (domain)
  → ItemResolver / InventoryResolver (domain validation)
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
│   ├── AppConfig.h              ← Application-level config (env-var backed)
│   └── BuildConfig.h.in         ← CMake-generated compile-time config (version, feature toggles)
├── cli/                         ← CLI config + parsing (CLIConfig, EnchantmentSpec, TargetSpec)
│   ├── cli.h/.cpp               ← parse_cli(), build_target(), build_enchset(), apply_config_pairs()
│   └── RegistryEditor.h/.cpp    ← apply_registry_edits() for --registry-edit
├── adapters/                    ← Domain ↔ compact / serialization boundary
│   ├── CompactAdapter.h/.cpp    ← apply / recall (domain ↔ compact)
│   ├── RawTypeAdapter.h/.cpp    ← Raw (string) ↔ Domain registries (resolve / revert)
│   ├── OutputFormatter.h/.cpp   ← EnchSolution → text/compact/json
│   └── EnchSerializer.h/.cpp    ← EnchInfo/Equipment ↔ JSON/CSV/MC official
├── types/                       ← Domain data types (pure data, no computation)
│   ├── CompactedTypes.h/.cpp    ← Compact types (namespace compact): Ench, EnchSet, Item, EnchStep
│   ├── EnchInfo.h/.cpp          ← Full enchantment definition
│   ├── EnchSet.h/.cpp           ← EnchSet container
│   ├── Ench.h                   ← Enchantment ID + level pair
│   ├── ItemStack.h/.cpp         ← Item with enchantments
│   ├── Equipment.h/.cpp         ← Equipment definition
│   ├── EnchSolution.h/.cpp      ← Solution type
│   ├── Platform.h               ← MCE enum (Minecraft edition Java/Bedrock/All)
│   ├── AlgorithmTypes.h         ← AlgorithmInput, AlgorithmOutput, ProgressStatus, AlgorithmState
│   ├── LogTypes.h               ← LogLevel, LogEntry
│   ├── RawTypes.h               ← String-based intermediates (RawEnchInfo, RawEquipment)
│   └── EquipmentCategory.h      ← Equipment category constants
├── log/                         ← Global async Logger (singleton; uses LogLevel/types)
│   ├── Logger.h/.cpp            ← Global async Logger. Built on EventLoop data
│   │                               mode (SegmentedMPSCQueue + FileHandler).
│   └── log.hpp                  ← Free-function wrappers + LOG_INFO / LOG_WARN macros
├── resolvers/                   ← Domain-level input resolution
│   ├── ItemResolver.h/.cpp      ← Direct-mode: applicability, conflict, diff, book generation
│   ├── InventoryResolver.h/.cpp ← Inventory-mode: parse JSON inventory, resolve IDs, sort by priority
│   └── TagResolver.hpp          ← Tag reference (#tag) resolution (for MC official format)
├── registries/                  ← Domain registries
│   ├── EnchantmentRegistry.h/.cpp  ← Full enchantment registry with subset derivation
│   ├── EquipmentRegistry.h/.cpp
│   ├── EquipmentCategoryRegistry.h/.cpp
│   ├── AlgorithmRegistry.h/.cpp ← Algorithm factory
│   ├── CompactedRegistries.h/.cpp ← EnchReg: compact subset with O(1) conflict matrix
│   ├── RegistryAccess.h         ← Meyer's singleton accessors
│   └── RegistryManager.h/.cpp   ← Registry data source management (discovery, loading, filtering)
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
│   ├── diagnostics/             ← Async diagnostics pipeline (event-driven)
│   │   ├── AlgorithmObserver.h  ← Streaming callback interface
│   │   ├── DiagnosticsService.h/.cpp ← Event dispatch + observer notification
│   │   ├── DiagnosticsWriter.h/.cpp  ← Persist-to-disk
│   │   └── AlgorithmDiagnostics.h/.cpp ← Exit diagnostics struct hierarchy
│   ├── components/              ← Algorithm building blocks
│   │   ├── Heuristic.h          ← Pool-based admissible heuristic
│   │   ├── HeuristicBasic.h     ← Direct Item-vector heuristic (no ItemPool dep)
│   │   ├── SearchUtils.h        ← fill_max_levels / compute_h helpers
│   │   ├── StateHash.h          ← State hashing utilities
│   │   └── ItemPool.h           ← Hash-dedup item pool (backed by MemoryPool)
│   ├── serialization/           ← Checkpoint: binary serialization for algorithm state
│   │   ├── IAlgorithmSerializer.h/.cpp ← Base class: file header + common sections + CRC
│   │   ├── CompactSerializer.h/.cpp    ← Compact type read/write primitives (Ench, Item, …)
│   │   └── CMakeLists.txt
│   ├── forge/
│   │   ├── IForgeEngine.h       ← Virtual interface + default sub-ops
│   │   └── ForgeEngine.h/.cpp   ← Vanilla implementation
│   └── strategies/              ← Algorithm strategy implementations
│       ├── greedy/              GreedyAlgorithm (fast approximate)
│       ├── diff_first/          DiffFirstAlgorithm (difficulty_first, PPN-layer merge)
│       ├── hamming/             HammingAlgorithm (popcount-balanced tree)
│       ├── hierarchical/        HierarchicalMergeAlgorithm (large-scale approx)
│       ├── penalty_balance/     DynamicPenaltyBalancingAlgorithm
│       ├── dfs/                 DFSAlgorithm (branch-and-bound + heuristic)
│       ├── astar/               AStarAlgorithm (exact optimal + ItemPool)
│       │   ├── AStarAlgorithm.* / AStarDiagnostics.* / AStarMemoryBudget.*
│       └── idastar/             IDAStarAlgorithm (TTTable, memory-efficient)
│           ├── IDAStarAlgorithm.* / IDAStarDiagnostics.*
│           └── TTTable.h        ← Epoch-based transposition table
├── utils/                       ← Generic utilities, zero project dependencies
│   ├── ParserUtils.hpp          ← String/JSON/file helpers (general)
│   ├── EnvUtil.hpp              ← Env-var access (get_env<T>)
│   ├── ExpCalculator.hpp        ← Level ↔ XP conversion
│   ├── HashUtils.hpp            ← hash_combine()
│   ├── FlatHashMap.hpp          ← Open-addressing hash map
│   ├── MemoryPool.hpp           ← PMR monotonic buffer memory resource
│   ├── ObjectPool.hpp           ← Fixed-size freelist object pool
│   ├── EventLoop.hpp            ← Zero-CPU-idle event loop (atomic::wait).
│   │                               Callable mode (default) + Data mode
│   │                               (compile-time via Handler template param)
│   └── queue/                   ← Lock-free queue family (Vyukov sequence numbers)
│       ├── IQueue.h             ← Virtual queue interface + QueueType concept
│       ├── BoundedMPMCQueue.hpp ← MPMC bounded
│       ├── BoundedMPSCQueue.hpp ← MPSC bounded
│       ├── SegmentedMPMCQueue.hpp ← MPMC unbounded
│       ├── SegmentedMPSCQueue.hpp ← MPSC unbounded
│       ├── SPSCQueue.hpp        ← SPSC bounded
│       └── SPMCQueue.hpp        ← SPMC bounded
├── io/                          ← I/O primitives (leaf, zero project deps)
│   ├── json.h/.cpp              ← Generic JSON DOM (parse + serialize)
│   ├── CsvIO.h/.cpp             ← CSV parse / format / write
│   └── ByteStream.h             ← Binary byte I/O (u8/16/32/64, varint, string)
├── data/
│   ├── CMakeLists.txt           ← Embeds vanilla.json into binary at compile time
│   └── builtin/vanilla.json     ← Built-in Vanilla Minecraft enchantment data
└── BESQTypes.h                  ← Umbrella include for domain types
```

See `docs/MPMCQueue.md` for the full queue design documentation.
Per-layer READMEs (architecture, API reference, development guide):
`src/types/`, `src/utils/`, `src/parsers/`, `src/adapters/`, `src/algorithm/serialization/`,
`src/registries/`, `src/io/`, `src/log/`, and `src/algorithm/` (with sub-directories).

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
