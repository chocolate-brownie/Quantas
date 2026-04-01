# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What Is QUANTAS

QUANTAS (Quantitative User-friendly Adaptive Networked Things Abstract Simulator) is a round-based, abstract simulator for studying distributed algorithms. It decouples algorithm logic from network stacks and OSes. Experiments are configured via JSON and executed as synchronous rounds with configurable message delay, loss, duplication, and reordering. Byzantine fault injection is supported via a modular `Fault` strategy system.

## Build & Run Commands

Requires **g++ 9+** (or `clang++` on macOS). C++17.

```sh
make release                          # optimized build
make debug                            # debug build with assertions
make run                              # build + run default input (set INPUTFILE in makefile)
make run INPUTFILE=quantas/PBFTPeer/PBFTInput.json   # run specific experiment
make clang                            # macOS: build with clang++ and run
make clean                            # remove .o, .exe, .out, etc.
make clean_txt                        # remove generated .txt log files
```

### Debugging & Memory

```sh
make run_memory                       # full valgrind leak check
make run_simple_memory                # valgrind summary only
make run_debug                        # run under gdb with auto-backtrace
```

### Tests

```sh
make test                             # runs rand_test + valgrind on all algorithm inputs
make rand_test                        # thread-based RNG correctness test only
```

There is no unit test framework — `make test` runs each algorithm's input JSON through valgrind and checks for memory leaks.

## Architecture

### Two Simulation Modes

- **Abstract** (`quantas/Common/Abstract/`): Round-based message passing over logical channels. This is the primary mode.
- **Concrete** (`quantas/Common/Concrete/`): Socket-based simulation (incomplete/experimental).

### Core Runtime (all in `quantas/Common/`)

| File | Role |
|------|------|
| `Peer.hpp` | Base class for all algorithms; also houses `PeerRegistry` (string→factory map) |
| `ByzantinePeer.hpp` | Extends `Peer` with `FaultManager` for hook-based fault injection |
| `Faults.hpp` | `Fault` base class with overridable hooks: `onSend`, `onReceive`, `onUnicastTo`, `onPerformComputation` |
| `Packet.hpp` | In-flight message wrapper (`getMessage()`, `sourceId()`, `targetId()`) |
| `NetworkInterface.hpp` | Abstract interface for send/receive; subclassed by Abstract and Concrete variants |
| `Abstract/Channel.hpp` | Models a directed link with delay/drop/duplicate/reorder semantics |
| `Abstract/Network.hpp` | Topology builder — creates peers and channels from JSON config |
| `Abstract/Simulation.hpp` | Main loop: iterates rounds, dispatches `performComputation()` per peer |
| `RoundManager.hpp` | Global round counter (`currentRound()`, `lastRound()`) |
| `LogWriter.hpp` | Structured JSON metric output (`pushValue(key, val)`, `print()`) |
| `BS_thread_pool.hpp` | Bundled thread pool for parallel round execution |
| `Json.hpp` | Bundled nlohmann/json |

### Algorithm Structure

Each algorithm lives in `quantas/<Name>/` and contains:
- `<Name>.hpp` / `<Name>.cpp` — peer subclass + `PeerRegistry` registration via static initializer
- `<Name>Input.json` — experiment config referencing the `.cpp` in its `algorithms` array

The makefile parses the JSON `algorithms` array to determine which `.cpp` files to compile. Changing `INPUTFILE` changes which algorithm gets built.

### Implemented Algorithms

`ExamplePeer`, `AltBitPeer` (alternating bit protocol), `PBFTPeer`, `RaftPeer`, `BitcoinPeer`, `EthereumPeer`, `KademliaPeer`, `LinearChordPeer`, `StableDataLinkPeer`

### Adding a New Algorithm

1. Create `quantas/YourPeer/YourPeer.hpp` and `.cpp` — subclass `Peer` (or `ByzantinePeer` for fault support)
2. Register with `PeerRegistry::registerPeerType("YourPeer", factory_lambda)` via static initializer in the `.cpp`
3. Override `performComputation()` (per-round logic), `initParameters()` (JSON config), `endOfRound()` (metrics)
4. Create a JSON input file listing your `.cpp` in `algorithms` and defining experiments
5. Build/run: `make run INPUTFILE=quantas/YourPeer/YourInput.json`

### Byzantine Fault System

Derive from `ByzantinePeer` instead of `Peer`. In `initParameters`, call `addFault(new YourFault(...))` on selected peers. Built-in faults: `EquivocateFault` (PBFT equivocation), `ParasiteFault` (selfish mining). Custom faults override hooks in `Fault` base class.

### Build System Quirk

The makefile uses `sed` to extract the `algorithms` array from the input JSON at build time, so the JSON must be well-formed and `INPUTFILE` must point to a valid file even for `make clean` to work cleanly after a build.
