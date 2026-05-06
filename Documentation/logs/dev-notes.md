## Bigger goals

1.  At one point Quantas's logger should act as Mq verison's leader
2.  at one point when I run make mqrun it should do exactly what quantas does right now instead of channel deque using MQ and as well as should run the logger as the leader process and should run N number of peer processes

Fundamental truth: that `for (expIndex...)` loop is one **experiment lifecycle orchestrator**.  
Everything inside it is one of a few jobs.

**Experiment Loop Jobs (first-principles decomposition)**

- [x] **J1: Select experiment config**
    - Read `config["experiments"][expIndex]`.
    - Validate required fields (`topology`, `initialPeerType`).
    - MQ status: done (you now iterate experiments and parse per-experiment basics).

- [ ] **J2: Configure coordinator for this experiment**
    - Tell coordinator experiment context, role, topology, peer count, stop policy, logging base.
    - TCP does this via `configureProcess(...)`.
    - MQ status: missing (MQ coordinator has no equivalent experiment-context config).

- [ ] **J3: Acquire peer assignments for this process**
    - Get which peer IDs this process owns + neighbor sets.
    - TCP does `waitForAssignments()`.
    - MQ status: missing (currently peer ID is CLI, neighbors are synthetic complete-graph-minus-self).

- [~] **J4: Construct local peers and bind network interfaces**
    - Create peer objects from type.
    - Attach concrete interface and configure neighbors.
    - MQ status: partially done (single peer path works; no assignment-driven multi-peer path).

- [ ] **J5: Resolve output/log destination for this experiment**
    - Compute experiment-specific output file path.
    - Configure writer.
    - MQ status: missing.

- [ ] **J6: Handle empty/invalid assignment fast-path**
    - If no local peers assigned, cleanup and continue.
    - MQ status: missing.

- [ ] **J7: Run experiment-level initialization hooks**
    - `initParameters(localPeers, experiment["parameters"])`.
    - Handle tests semantics (`tests > 1` warning/behavior).
    - MQ status: missing (currently no `initParameters` call in MQ worker).

- [~] **J8: Start synchronization gate**
    - Mark ready, wait for start signal.
    - TCP: `markReady()` + `waitForStartSignal()`.
    - MQ: `sendReady()` + `waitForStart()` exists, but only follower-side with external leader flow.

- [ ] **J9: Execute main run loop under stop policy**
    - Loop until coordinator stop condition, not fixed rounds only.
    - For each local peer: receive + compute.
    - MQ status: missing parity (currently fixed-round loop only).

- [ ] **J10: Execute per-round global hook**
    - Call `endOfRound(localPeers)` once per round.
    - MQ status: missing (critical parity gap for metrics behavior).

- [ ] **J11: Execute end-of-experiment hook**
    - `endOfExperiment(localPeers)`.
    - MQ status: missing.

- [ ] **J12: Stop handshake completion**
    - Wait for authoritative stop confirmation.
    - MQ status: missing (`notifyPeerStopped/broadcastStop/waitForStop` absent).

- [ ] **J13: Emit final experiment metrics**
    - Runtime, peak memory, output print.
    - MQ status: missing.

- [~] **J14: Teardown experiment resources**
    - Clear interfaces, delete peers, coordinator cleanup.
    - MQ status: partially done (peer cleanup done; leader-driven full lifecycle cleanup not integrated in same runtime).

---

**What this means for your MQ design (next small functions to implement)**

1. `ExperimentConfig parseExperiment(config, expIndex, roundsOverride)` ✅ (already started)
2. `void configureMqExperimentCoordinator(...)`
3. `std::vector<Assignment> getMqAssignments(...)` (or single-peer shim first)
4. `std::vector<Peer*> buildLocalPeers(...)`
5. `void initExperimentHooks(...)` (`initParameters`)
6. `void synchronizeStart(...)`
7. `RunStats runUntilStop(...)` (or temporary fixed-round variant)
8. `void finalizeExperimentHooks(...)` (`endOfRound/endOfExperiment` integration path)
9. `void writeExperimentOutputs(...)`
10. `void cleanupExperimentState(...)`

If you follow this checklist, you’ll converge to TCP parity systematically instead of patching ad hoc behavior.
