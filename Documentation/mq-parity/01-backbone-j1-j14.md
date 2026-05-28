## 🎯 Bigger Goals

1. Logger should act as MQ version's leader/coordinator.
2. `make mqrun` should match current QUANTAS lifecycle semantics (using MQ transport), launching leader + N peer processes.
3. If we need to compare the MQ version's performance with other network interfaces, we should add timing/observability intentionally (not ad hoc).

---

## 🧭 Scope Clarification

- This file tracks **MQ backend parity against original QUANTAS behavior** (README-defined behavior).
- TCP work is being developed in a separate branch by a colleague and is **not** the delivery target for this backlog.
- Target architecture direction: QUANTAS supports multiple concrete network layers (MQ now, TCP alongside, ZeroMQ/Mininet in future) while preserving the same experiment semantics.

---

## 🧠 First-Principles Parity Contract

To claim “MQ replaced the original network layer behavior,” these invariants must hold:

1. **Input semantics parity**
   - Same JSON meaning for `topology`, `distribution`, `tests`, `rounds`, `parameters`, `logFile`.
2. **Lifecycle parity**
   - Same experiment loop shape: configure -> assign -> build -> init -> start gate -> rounds -> hooks -> stop -> output -> cleanup.
3. **Termination parity**
   - Stop is protocol-authoritative, not local guesswork.
4. **Output parity**
   - Final experiment artifacts are valid and interpretable under the same expectations as baseline QUANTAS.
5. **Isolation parity**
   - Experiments and processes do not corrupt each other’s state/artifacts.

---

## 📊 Current Verdict (MQ)

- **Execution path:** working baseline.
- **Transport:** Boost message queues are functioning for peer messaging and leader/follower control-plane.
- **Lifecycle:** J1–J14 baseline mostly wired.
- **Full behavioral parity with original QUANTAS:** **not complete yet**.

Reason: topology/distribution/tests semantics are not fully aligned with README contract yet.

---

## ✅ Experiment Lifecycle Jobs (J1–J14)

- [x] **J1: Select experiment config**
  - Read `config["experiments"][expIndex]`.
  - Validate required fields (`topology`, `initialPeerType`).
  - MQ status: ✅ done.

- [x] **J2: Configure coordinator for this experiment**
  - Provide experiment context, role, topology, peer count, stop policy, log base.
  - MQ status: ✅ done via `configureExperiment(...)`.

- [x] **J3: Acquire peer assignments for this process**
  - Determine owned peer IDs + neighbor sets.
  - MQ status: ✅ done for phase-1 local ownership shape.

- [x] **J4: Construct local peers and bind network interfaces**
  - Build peer objects and attach configured interfaces.
  - MQ status: ✅ done (assignment-list/localPeers loop shape).

- [x] **J5: Resolve output/log destination for this experiment**
  - Compute experiment output path and configure writer.
  - MQ status: ✅ done.
  - Note: fixed output-base resolution from `logFile` and per-peer file disambiguation to avoid file corruption.

- [x] **J6: Handle empty/invalid assignment fast-path**
  - Skip safely when no runnable local peers.
  - MQ status: ✅ done.

- [x] **J7: Run experiment-level initialization hooks**
  - `initParameters(localPeers, experiment["parameters"])`.
  - MQ status: ✅ done (with current `tests > 1` warning behavior).

- [x] **J8: Start synchronization gate**
  - `sendReady()` + `waitForStart()` + leader `waitForAllReady()` + `broadcastStart()`.
  - MQ status: ✅ done baseline.

- [x] **J9: Execute main run loop under stop policy**
  - Per local peer: `receive()` + `tryPerformComputation()`.
  - MQ status: ✅ baseline done.
  - Note: `DoneSignals` still has fallback logic and is not yet final policy parity.

- [x] **J10: Execute per-round global hook**
  - `endOfRound(localPeers)` once per round.
  - MQ status: ✅ done.

- [x] **J11: Execute end-of-experiment hook**
  - `endOfExperiment(localPeers)`.
  - MQ status: ✅ done.

- [x] **J12: Stop handshake completion (baseline)**
  - `notifyPeerStopped` / leader done collection / `broadcastStop` / `waitForStop` wired.
  - MQ status: ✅ baseline done.
  - Caveat: final stop-policy parity (`DoneSignals` semantics) still needs tightening.

- [x] **J13: Emit final experiment metrics (baseline)**
  - Runtime + peak memory + final output print wired in MQ worker lifecycle.
  - MQ status: ✅ baseline done.
  - Caveat: artifact policy (per-peer vs aggregate) needs explicit product decision.

- [x] **J14: Teardown experiment resources (baseline)**
  - Peer/interface cleanup + coordinator cleanup in success/skip/failure paths.
  - MQ status: ✅ baseline done.
  - Caveat: continue validating multi-experiment runs and queue cleanup robustness.

---

## 🐛 Known Bug Fixes Landed

- `_GLIBCXX_DEBUG` + Boost serialization mismatch on MQ debug targets.
  - Fix: removed `_GLIBCXX_DEBUG` from `mq_peer_debug` and `mq_leader_debug`.

- Blocking MQ `send(...)` under queue pressure.
  - Fix: `timed_send(...)` + per-destination drop counters.

- Start/stop trigger ambiguity in control messages.
  - Fix: explicit trigger separation for ready/start/stop/done.

- Shared file corruption from multi-peer writes.
  - Fix: per-peer output filename disambiguation (`..._p<peerId>`).

---

## 🧠 What Is Still Missing For Full QUANTAS Parity

### 1) Topology semantics parity (critical)
- Current MQ assignment path still uses phase-1 simplified neighbor construction and does not fully implement README topology contract (`star/grid/torus/ring/userList/...`).

### 2) Distribution/channel semantics parity (critical)
- Original QUANTAS channel model fields (`dropProbability`, `duplicateProbability`, `reorderProbability`, delay models, queue-size semantics) are not yet faithfully mapped into MQ concrete runtime behavior.

### 3) Test-loop semantics parity (`tests > 1`)
- MQ currently warns and effectively runs single-test behavior in this path; README semantics expect repeated test execution.

### 4) Stop policy parity hardening
- Baseline stop handshake exists, but policy behavior around `DoneSignals` should be finalized so stop semantics are protocol-driven without fallback artifacts.

### 5) Artifact policy decision
- Decide expected canonical output contract for MQ runs:
  - per-peer files,
  - aggregate experiment file,
  - or both.

---

## 🔍 Updated Focus (Next Milestones)

- Current focus: **semantic parity gaps** after baseline lifecycle completion.

### Milestone M1: Topology parity
- Implement topology-faithful assignment/wiring in MQ path to match README behavior.
- Validation:
  - run at least `complete`, `ring`, `grid`, and `userList` inputs,
  - verify neighbor sets/behavior match expected graph semantics.

### Milestone M2: Distribution/channel parity
- Define and implement mapping of QUANTAS distribution model to MQ transport behavior.
- Validation:
  - controlled experiments for drop/reorder/duplicate/delay effects,
  - compare expected statistical behavior vs baseline channel model intent.

### Milestone M3: Tests semantics parity
- Support `tests > 1` lifecycle and output semantics in MQ worker path.
- Validation:
  - repeated test outputs appear correctly and consistently.

### Milestone M4: Stop-policy finalization
- Remove/resolve fallback semantics and make `DoneSignals` policy explicit.
- Validation:
  - protocol-driven shutdown behavior with stable logs and exit codes.

### Milestone M5: Artifact contract finalization
- Decide and implement canonical report structure for MQ experiments.
- Validation:
  - outputs are deterministic, non-corrupted, and easy to compare across backends.

---

## 🧩 MQ Design Checklist (Updated)

1. `ExperimentConfig parseExperiment(config, expIndex, roundsOverride)` ✅
2. `configureMqExperimentCoordinator(...)` ✅
3. `getMqAssignments(...)` → topology-faithful version ❌
4. `buildLocalPeers(...)` ✅
5. `initExperimentHooks(...)` ✅
6. `synchronizeStart(...)` ✅
7. `runUntilStop(...)` ✅ baseline, ❌ policy-final form
8. `finalizeExperimentHooks(...)` ✅
9. `writeExperimentOutputs(...)` ✅ baseline, ❌ final artifact contract
10. `cleanupExperimentState(...)` ✅ baseline

---

## 🚧 Incremental Phase Remaining (Strict Order)

- [x] **I1: Finish J4 shape**
- [x] **I2: Add experiment hooks (J7/J10/J11)**
- [x] **I3: Add minimal stop handshake (J12 baseline)**
- [x] **I4: Add output/metrics basics (J13 baseline)**
- [x] **I5: Tighten lifecycle cleanup (J14 baseline)**

### New semantic-parity phase

- [ ] **I6: Topology parity implementation (README-complete)**
- [ ] **I7: Distribution/channel parity implementation**
- [ ] **I8: `tests > 1` parity implementation**
- [ ] **I9: Stop-policy hardening (`DoneSignals` final semantics)**
- [ ] **I10: Output artifact contract finalization**

