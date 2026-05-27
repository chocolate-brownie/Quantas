## 🎯 Bigger Goals

1. Logger should act as MQ version's leader/coordinator.
2. `make mqrun` should match current QUANTAS lifecycle semantics (using MQ transport), launching leader + N peer processes.
3. If we need to compare the mq version's performance with other network interfaces we have to attatch some timing and logging which I have skipped for the moment

---

## 🧭 Core Framing

The `for (expIndex...)` loop is one **experiment lifecycle orchestrator**.
Each lifecycle job below should reach parity with TCP concrete runtime.

---

## ✅ Experiment Lifecycle Jobs (J1–J14)

- [x] **J1: Select experiment config**
    - Read `config["experiments"][expIndex]`.
    - Validate required fields (`topology`, `initialPeerType`).
    - MQ status: ✅ done.

- [x] **J2: Configure coordinator for this experiment**
    - Provide experiment context, role, topology, peer count, stop policy, log base.
    - TCP equivalent: `configureProcess(...)`.
    - MQ status: ✅ done via `configureExperiment(...)` in `ConcreteMqPeer.cpp`.

- [x] **J3: Acquire peer assignments for this process**
    - Determine owned peer IDs + neighbor sets.
    - TCP equivalent: `waitForAssignments()`.
    - MQ status: ✅ done (phase-1 local assignment via `MqAssignment` + validation + apply).

- [x] **J4: Construct local peers and bind network interfaces**
    - Build peer objects and attach configured interfaces.
    - MQ status: ✅ done for incremental phase (assignment-list/localPeers loop shape).

- [x] **J5: Resolve output/log destination for this experiment**
    - Compute experiment output path and configure writer.
    - MQ status: ✅ done (`makeExperimentFileName(...)` + `LogWriter::setLogFile(...)`).

- [x] **J6: Handle empty/invalid assignment fast-path**
    - Skip safely when no runnable local peers.
    - MQ status: ✅ done (`prepareLocalPeers(...)` gate + cleanup).

- [x] **J7: Run experiment-level initialization hooks**
    - `initParameters(localPeers, experiment["parameters"])`.
    - Warn for `tests > 1` behavior.
    - MQ status: ✅ done (`initializeHooks(...)`).

- [x] **J8: Start synchronization gate**
    - Mark ready, wait for start.
    - TCP: `markReady()` + `waitForStartSignal()`.
    - MQ: `sendReady()` + `waitForStart()`.
    - MQ status: ✅ done baseline.

- [x] **J9: Execute main run loop under stop policy**
    - Loop based on coordinator stop policy (not only fixed-round loop structure).
    - Per local peer: `receive()` + `tryPerformComputation()`.
    - Added coordinator mode branching (`FixedRounds` / `DoneSignals`) with temporary DoneSignals fallback.
    - Added observability improvements:
        - stop-request logs now include requesting peer id,
        - per-peer loop exit summary logs include `mode`, `loopCount`, `currentRoundView`, and `reason`.
    - Validation evidence:
        - `make -j4 mq_peer_debug mq_leader_debug` ✅
        - `make mq_run_all INPUTFILE=quantas/BitcoinPeer/BitcoinPeerInput.json MQ_TOTAL_PEERS=11 MQ_ROUNDS=5` ✅
        - all peers + leader exit code `0`; stop reason logged as `fixed_rounds_reached`.
    - MQ status: ✅ **baseline done**.
    - ⚠️ Note: global done-signal propagation is still pending J12.

- [x] **J10: Execute per-round global hook**
    - `endOfRound(localPeers)` once per round.
    - MQ status: ✅ done (wired in `runRounds(...)` via `localPeers.front()->endOfRound(localPeers)` once per loop iteration).

- [x] **J11: Execute end-of-experiment hook**
    - `endOfExperiment(localPeers)`.
    - MQ status: ✅ done (wired after rounds complete via `localPeers.front()->endOfExperiment(localPeers)`).

- [ ] **J12: Stop handshake completion**
    - Wait for authoritative stop confirmation.
    - MQ status: ❌ missing (`notifyPeerStopped` / `broadcastStop` / `waitForStop` equivalents absent).

- [ ] **J13: Emit final experiment metrics**
    - Runtime, peak memory, final output print parity.
    - MQ status: ❌ missing.

- [~] **J14: Teardown experiment resources**
    - Clear interfaces, delete peers, coordinator cleanup.
    - MQ status: 🟡 partial (peer cleanup exists; leader/follower coordinated cleanup incomplete).

---

## 🐛 Known Bug Fixes Already Landed

- Root cause 1: `_GLIBCXX_DEBUG` + Boost serialization mismatch on MQ debug targets.
    - Fix: removed `_GLIBCXX_DEBUG` from `mq_peer_debug` and `mq_leader_debug`.

- Root cause 2: blocking MQ `send(...)` under queue pressure.
    - Fix: `timed_send(...)` + per-destination drop counters.

- Validation:
    - `make mq_run_all_debug_peer ... MQ_TOTAL_PEERS=11 MQ_ROUNDS=10`
    - ✅ all exit codes `0`, no segfault/hang.

---

## 🧠 Metric-Parity Guardrail

Do **not** serialize full `Peer` objects as default metric-parity strategy.
That approach is high-risk for phase 1 (polymorphism + process-local state + backend coupling).

Preferred direction:

1. Split parity tracks:
    - **Execution parity first** (start/round/stop lifecycle correctness)
    - **Metric parity second** (`endOfRound` / `endOfExperiment` correctness)
2. For metric parity, use a minimal explicit snapshot schema with hook-relevant fields.

---

## 🔍 Current Focus

- Current focus: **I3 (J12)** stop-handshake completion, then **I4 (J13)** output/metrics parity.

---

## 🧩 MQ Design Checklist (Implementation Helpers)

1. `ExperimentConfig parseExperiment(config, expIndex, roundsOverride)` ✅
2. `void configureMqExperimentCoordinator(...)`
3. `std::vector<Assignment> getMqAssignments(...)` (or single-peer shim first)
4. `std::vector<Peer*> buildLocalPeers(...)`
5. `void initExperimentHooks(...)` (`initParameters`)
6. `void synchronizeStart(...)`
7. `RunStats runUntilStop(...)` (or temporary fixed-round variant)
8. `void finalizeExperimentHooks(...)` (`endOfRound` / `endOfExperiment` path)
9. `void writeExperimentOutputs(...)`
10. `void cleanupExperimentState(...)`

If this checklist is followed, parity converges systematically (instead of ad hoc patching).

---

## 🚧 Incremental Phase Remaining (Strict Order)

- [x] **I1: Finish J4 shape**
    - Move from single `Peer*` flow to assignment-list/localPeers loop shape.

- [x] **I2: Add experiment hooks (J7/J10/J11)**
    - Wire `initParameters(...)`, per-round `endOfRound(...)`, and `endOfExperiment(...)`.

- [ ] **I3: Add minimal stop handshake (J12 baseline)**
    - Add MQ done/stop signals so shutdown is protocol-controlled, not only local-round controlled.

- [ ] **I4: Add output/metrics basics (J5/J13 baseline)**
    - J5 is done; remaining work is J13 runtime/memory/final output parity.

- [ ] **I5: Tighten lifecycle cleanup (J14)**
    - Make experiment cleanup explicitly coordinated across leader/follower roles.
