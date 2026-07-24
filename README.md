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

The project uses a **four-domain architecture** built on shared utilities. The algorithm domain uses compact types (`namespace algorithm`) for hot paths, while business/interface domains use domain types for I/O boundaries. The algorithm layer has zero domain type dependencies.

```
CLI → Interface parsers (domain)
  → Interface resolvers (domain validation)
  → CompactAdapter::apply() (validates + prunes EnchReg + converts)
  → AlgorithmInput (compact) → AlgorithmExecutor → IAlgorithm
  → AlgorithmOutput (compact steps)
  → CompactAdapter::recall() (restores IDs + builds solutions)
  → OutputFormatter (domain)
```

### Source Layout

```
src/
├── main.cpp                             ← Entrypoint
├── BESQTypes.h                          ← Umbrella include
├── common/                              ← Shared utilities (zero domain deps)
│   ├── io/                              ← JSON DOM, CSV reader/writer, ByteStream
│   ├── log/                             ← Global async Logger (SegmentedMPSCQueue + EventLoop)
│   │   ├── Logger.h/.cpp                ← Logger + LOG_INFO / LOG_WARN macros
│   │   └── log.hpp                      ← Free-function wrappers
│   └── utils/                           ← Generic utilities
│       ├── ParserUtils.hpp              ← String/JSON/file helpers
│       ├── EnvUtil.hpp                  ← Env-var access (get_env<T>)
│       ├── ExpCalculator.hpp            ← Level ↔ XP conversion
│       ├── HashUtils.hpp                ← hash_combine()
│       ├── FlatHashMap.hpp              ← Open-addressing hash map
│       ├── MemoryPool.hpp               ← PMR monotonic buffer
│       ├── ObjectPool.hpp               ← Fixed-size freelist pool
│       ├── EventLoop.hpp                ← Zero-CPU-idle event loop
│       └── queue/                       ← Lock-free queue family
│           ├── IQueue.h                 ← Virtual interface + QueueType concept
│           ├── BoundedMPMCQueue.hpp      ← MPMC bounded
│           ├── BoundedMPSCQueue.hpp      ← MPSC bounded
│           ├── SegmentedMPMCQueue.hpp    ← MPMC unbounded
│           ├── SegmentedMPSCQueue.hpp    ← MPSC unbounded
│           ├── SPSCQueue.hpp             ← SPSC bounded
│           └── SPMCQueue.hpp             ← SPMC bounded
├── domain/
│   ├── algorithm/                       ← Algorithm domain (zero business/interface deps)
│   │   ├── IAlgorithm.h                 ← AlgorithmInput/Output + IAlgorithm interface
│   │   ├── AlgorithmExecutor.h/.cpp     ← Async engine (thread lifecycle + observer)
│   │   ├── ExecutionContext.h/.cpp      ← Cancel/pause/progress + solution accumulator
│   │   ├── types/                       ← Compact types + config types
│   │   │   ├── CompactedTypes.h/.cpp    ← Ench, EnchSet, Item, EnchStep (namespace algorithm)
│   │   │   ├── AlgorithmTypes.h         ← AlgorithmInput, AlgorithmOutput, AlgorithmState
│   │   │   └── ConfigTypes.h            ← ForgeConfig, SearchConfig
│   │   ├── registries/                  ← Compact EnchReg (O(1) conflict matrix)
│   │   │   └── EnchReg.h/.cpp           ← N×N vector<char> conflict matrix
│   │   ├── forge_engine/                ← Forge engine (virtual + vanilla impl)
│   │   │   ├── IForgeEngine.h           ← Virtual interface + default sub-ops
│   │   │   └── ForgeEngine.h/.cpp       ← Vanilla implementation
│   │   ├── _strategies/                 ← Algorithm strategy implementations
│   │   │   ├── greedy/                  GreedyAlgorithm (fast approximate)
│   │   │   ├── dfs/                     DFSAlgorithm (B&B + hash memoization)
│   │   │   ├── astar/                   AStarAlgorithm (exact optimal)
│   │   │   ├── idastar/                 IDAStarAlgorithm (TTTable, memory-efficient)
│   │   │   ├── hamming/                 HammingAlgorithm (popcount-balanced tree)
│   │   │   ├── hierarchical/            HierarchicalMergeAlgorithm (large-scale approx)
│   │   │   ├── penalty_balance/         DynamicPenaltyBalancingAlgorithm
│   │   │   └── diff_first/              DiffFirstAlgorithm (PPN-layer merge)
│   │   ├── components/                  ← Algorithm building blocks
│   │   │   ├── Heuristic.h              ← Pool-based admissible heuristic
│   │   │   ├── HeuristicBasic.h         ← Direct Item-vector heuristic
│   │   │   ├── SearchUtils.h            ← fill_max_levels / compute_h helpers
│   │   │   ├── StateHash.h              ← State hashing utilities
│   │   │   └── ItemPool.h               ← Hash-dedup item pool (MemoryPool-backed)
│   │   ├── diagnostics/                 ← Event-driven diagnostics pipeline
│   │   │   ├── IAlgorithmObserver.h     ← Streaming callback interface
│   │   │   ├── DiagnosticsService.h/.cpp← Event dispatch + observer notification
│   │   │   ├── DiagnosticsWriter.h/.cpp ← Persist-to-disk
│   │   │   └── AlgorithmDiagnostics.h/.cpp ← Exit diagnostics struct hierarchy
│   │   ├── serialization/               ← Binary checkpoint for algorithm state
│   │   │   ├── IAlgorithmSerializer.h/.cpp ← Base class + common sections + CRC
│   │   │   └── CompactSerializer.h/.cpp ← Compact type read/write (Ench, Item, …)
│   │   ├── plugin/                      ← AlgorithmLoader (built-in + plugin hot-loading)
│   │   └── resolvers/                   ← Algorithm-level resolution helpers
│   ├── business/                        ← Business domain (types and registries)
│   │   ├── types/
│   │   │   ├── Item.h/.cpp              ← Item (was ItemStack)
│   │   │   ├── Ench.h                   ← Enchantment ID + level pair
│   │   │   ├── EnchSet.h/.cpp           ← EnchSet container
│   │   │   ├── EnchInfo.h/.cpp          ← Full enchantment definition
│   │   │   ├── Equipment.h/.cpp         ← Equipment definition
│   │   │   ├── EquipmentTag.h           ← Equipment tag (was EquipmentCategory)
│   │   │   ├── Solution.h/.cpp          ← Solution type (was EnchSolution)
│   │   │   └── CommonTypes.h            ← MCE enum, NSID type
│   │   └── registries/
│   │       ├── EnchantmentRegistry.h/.cpp  ← Full enchantment registry (mutable)
│   │       ├── EquipmentRegistry.h/.cpp
│   │       ├── EquipmentTagRegistry.h/.cpp ← Equipment tag registry
│   │       └── RegistryManager.h/.cpp      ← Discovery/loading/filtering
│   ├── interface/                       ← Interface domain (I/O boundary)
│   │   ├── cli/                         ← CLI argument parsing
│   │   ├── parsers/                     ← File format parsers (auto-detect JSON/CSV/MC)
│   │   ├── api/                         ← Library API for external consumers
│   │   ├── abi/                         ← ABI stability layer
│   │   ├── types/                       ← Interface-level types
│   │   ├── components/                  ← Input validation, orchestrator helpers
│   │   └── fs/                          ← Filesystem operations
│   └── orchestration/                   ← Orchestration domain (cross-domain glue)
│       ├── components/
│       │   ├── CompactAdapter.h/.cpp    ← apply / recall (domain ↔ compact)
│       │   ├── RawTypeAdapter.h/.cpp    ← Raw (string) ↔ Domain registries
│       │   ├── OutputFormatter.h/.cpp   ← Solution → text/compact/json
│       │   └── EnchSerializer.h/.cpp    ← EnchInfo/Equipment ↔ JSON/CSV/MC official
│       └── types/                       ← Cross-domain result/error types
├── data/
│   ├── CMakeLists.txt                   ← Embeds vanilla.json at compile time
│   └── builtin/vanilla.json             ← Built-in Vanilla data
├── builtin/
│   └── DataLoader.h/.cpp                ← Built-in data loading

tests/                                   ← Per-component standalone test executables
benchmarks/                              ← Performance benchmarks
```

See `docs/MPMCQueue.md` for the full queue design documentation.

### Key Design Decisions

**Compact types (algorithm::)**: `algorithm::Ench` (4 bytes), `algorithm::EnchSet` (sorted vector, O(log N) lookup), `algorithm::Item` (type + dur + ppn + enchs). No pointer indirection, no virtual dispatch, zero domain dependencies.

**Flat conflict matrix**: `algorithm::EnchReg` stores an N×N `vector<char>` for O(1) incompatibility checks — single allocation, contiguous memory.

**EnchReg pruning**: `CompactAdapter::apply()` builds a subset of the global registry (via `CompactEnchInfo` construction) that only includes enchantments applicable to the target equipment. Smaller conflict matrix, faster lookups.

**IForgeEngine virtual interface**: All forge sub-operations (`penalty_cost`, `apply_cap`, `estimate_forge_cost`) have default vanilla implementations. Subclass only what you need for modded rules. `ForgeEngine` overrides to respect `ForgeConfig` flags. Book multiplier is precomputed as `algorithm::EnchInfo::mul_b` at data-load time.

**AlgorithmInput owns data**: `algorithm::EnchReg`, `algorithm::Item` vector, and target collection are stored by value — no pointers, no external lifetime dependencies. The struct owns two config sub-objects: `f_config` (forge config, `ForgeConfig`) and `s_config` (search config, `SearchConfig`).

**Domain types are pure data**: Business domain types (`EnchSet`, `Ench`, `Item`) are containers only. All combine/cost/penalty computation lives in the algorithm domain's forge engine.

**Four-domain layering**: `algorithm/` depends on nothing outside `common/`. `business/` adds domain types and registries. `interface/` adds CLI, parsers, API. `orchestration/` wires everything together via CompactAdapter and OutputFormatter.

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
