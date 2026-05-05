# TODO

## 1. Objective

For the current phase (single machine, multi-process), the specific target is:

1. run real QUANTAS peers as independent OS processes;
2. communicate through message passing (Boost message queues);
3. preserve abstract semantics sufficiently to compare outputs with abstract QUANTAS;
4. prepare protocol decisions that can migrate to ZeroMQ later.

This document summarizes what is implemented, what is missing, and an ordered plan to close the gap.

## 2. Baseline: What Abstract QUANTAS Does Today

Abstract mode (`quantas/Common/Abstract/Simulation.hpp`) is the reference behavior:

- reads one experiment config (`topology`, `parameters`, `tests`, `rounds`);
- builds all peers in one process using `Network::initNetwork(...)`;
- calls `initParameters(peers, params)` once on the full peer set;
- for each round: `receive(all peers)` -> `tryPerformComputation(all peers)` -> `endOfRound(full peers)`;
- collects metrics through `LogWriter`, then prints one experiment output.

Any ConcreteMQ backend that claims parity must define how these same lifecycle points are preserved across process boundaries.

## 3. Implemented in ConcreteMQ (Current State)

### 3.1 Communication Layer

`quantas/Common/ConcreteMQ/NetworkInterfaceConcreteMQ.{hpp,cpp}`:

- implemented `configure(id, neighbors)`;
- implemented `unicastTo(...)` with Boost serialization of `Packet` into POSIX MQ;
- implemented `receive()` draining inbox queue via `try_receive()`;
- prints per-packet latency to stdout.

Status: **working for peer-to-peer transport**.

### 3.2 Process Rendezvous (Start Barrier)

`quantas/Common/ConcreteMQ/ProcessCoordinatorMQ.{hpp,cpp}`:

- `createBarrier()`, `createInbox()`, `sendReady()`, `waitForAllReady()`, `broadcastStart()`, `waitForStart()`, `cleanUp()`.

Status: **working for basic startup synchronization**.

### 3.3 Peer Interface Swapping

`quantas/Common/Peer.hpp`:

- `setNetworkInterface(NetworkInterface*)` added, enabling reuse of existing algorithm peers with MQ interface injection.

Status: **critical architectural enabler completed**.

### 3.4 Real Algorithm Smoke Execution

`quantas/Common/ConcreteMQ/Tests/mq_peer_runner.cpp` + `Tests/makefile`:

- loads JSON (`experiments[0]`);
- creates one real algorithm peer by type string (`initialPeerType`);
- configures MQ interface and runs rounds;
- supports optional CLI override for rounds;
- tested with AltBit over two processes.

Status: **proof that real algorithm code can run over MQ transport**.

## 4. What Is Still Missing (Gap to Research Goal)

### 4.1 Missing Runtime Entry Point

`quantas/Common/ConcreteMQ/ConcreteSimulationMQ.cpp` and `.hpp` are empty stubs.  
There is no production ConcreteMQ simulation executable yet.

### 4.2 Coordinator Protocol Incomplete

Current `ProcessCoordinatorMQ` lacks:

- dynamic ID assignment protocol;
- per-round coordination semantics (if strict round lockstep is required);
- stop/done protocol (`ROUND_DONE`, `READY`, `STOP`) analogous to concrete TCP coordinator;
- robust multi-experiment lifecycle handling.

### 4.3 Lifecycle Parity Gaps vs Abstract

Current `mq_peer_runner` does not yet preserve full abstract lifecycle:

- `initParameters(full_peer_list, ...)` not handled with equivalent semantics;
- `endOfRound(full_peer_list)` not handled with equivalent semantics;
- `endOfExperiment(full_peer_list)` path missing in MQ test runner;
- topology handling currently simplified (neighbors built as “all others” in runner).

### 4.4 Metrics Parity Gaps

Current output is mostly latency lines on stdout.  
To compare against abstract output, MQ mode must produce equivalent experiment-level metrics in a stable output file format.

### 4.5 Scale and Robustness Risks

- POSIX queue capacity defaults (`fs.mqueue.msg_max`) can block larger scenarios;
- current test flow is mostly 2-peer smoke style;
- no validated Bitcoin parity run yet for abstract-vs-MQ comparison.

## 5. Ordered TODO Plan (Low -> High Complexity)

## Phase A — Stabilize Current Test Harness (Low)

1. Add lightweight per-peer summary in runner (sent/received totals, by direction, by action).
2. Ensure stale-queue cleanup is deterministic in every test target.
3. Add one reproducible “known good” AltBit command in docs.

Acceptance test:

```bash
make -C quantas/Common/ConcreteMQ/Tests run_mq_peer_runner MQ_ROUNDS=10
```

Expected: deterministic startup, message exchange, and end summary from both peers.

## Phase B — Topology and Config Correctness in Runner (Low-Medium)

1. Replace hardcoded neighbor derivation with topology-derived neighbors from JSON (`complete`, `ring`, `chain`, `userList` first).
2. Keep `initialPeers`, `initialPeerType`, `rounds` from JSON as single source of truth.

Acceptance test:

- run same algorithm with two different topologies and verify neighbor sets/traffic direction are consistent.

## Phase C — Coordinator Protocol Completion (Medium)

1. Extend `ProcessCoordinatorMQ` protocol:
   - peer registration;
   - dynamic ID assignment;
   - explicit `READY` and `START`;
   - explicit completion signaling (`ROUND_DONE` and/or `PEER_DONE`);
   - coordinator-driven `STOP`.
2. Define message schema now so the same protocol maps to ZeroMQ later.

Acceptance test:

- run N peers where process launch order is shuffled; all peers still receive unique IDs and start correctly.

## Phase D — Implement `ConcreteSimulationMQ.cpp` (Medium-High)

1. Create production entry point for MQ mode (not test-only binary).
2. Wire full experiment lifecycle:
   - experiment loop;
   - peer creation by registry type;
   - interface injection;
   - round loop;
   - stop condition;
   - final cleanup.
3. Add dedicated build target for MQ mode.

Acceptance test:

- one command should run MQ experiment end-to-end, without manual multi-terminal orchestration.

## Phase E — Metric and Lifecycle Parity with Abstract (High)

1. Define exact parity contract for:
   - `initParameters`,
   - `endOfRound`,
   - `endOfExperiment`.
2. Decide implementation strategy:
   - coordinator-side aggregation snapshots,
   - or controlled emulation of full-peer view for metric routines.
3. Emit one experiment output file comparable to abstract `LogWriter` output.

Acceptance test:

- same config in abstract and MQ should produce comparable trend-level metrics (not necessarily identical runtime values).

## Phase F — Bitcoin Comparative Benchmark (High)

1. Select manageable Bitcoin config (small N, reduced rounds first).
2. Run abstract baseline -> save output.
3. Run MQ backend with same config -> save output.
4. Produce comparison report:
   - correctness signals,
   - throughput/utility trends,
   - runtime and transport differences.

Acceptance test:

- one documented benchmark artifact (e.g., `bitcoinspeedtest.txt` + MQ counterpart) ready for supervisor review.

## 6. Suggested Working Rhythm (Slow, Problem-by-Problem)

For each TODO item:

1. write the smallest code change;
2. run one focused test command;
3. record observed output in `Documentation/logs/log.md`;
4. only then move to next item.

This keeps debugging local and prevents protocol/lifecycle issues from stacking.

## 7. Current Completion Estimate

Estimated progress toward “MQ backend comparable to abstract outputs”:

- **~55% complete** on bring-up and proof-of-feasibility;
- remaining **~45%** is lifecycle parity, coordinator protocol completion, production entrypoint, and benchmark-quality comparison output.

---

## Practical Next Step (Recommended)

Implement **Phase A item 1** (per-peer end summary counters) immediately.  
It is low risk, high diagnostic value, and directly reduces confusion before deeper protocol work.
