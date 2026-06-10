# ConcreteMQ Supervisor Questions

This file keeps the ConcreteMQ discussion focused on the few decisions that
unlock the rest of the work. The goal is not to decide every implementation
detail in the meeting, but to answer the questions that make the next coding
steps obvious.

Current supervisor decision:
- ConcreteMQ is a realistic process-based backend today.
- Strict Abstract lockstep parity is not currently implemented.
- Process orchestration stays in the root `makefile` for now; a C++ process
  manager is not part of the current MQ completion scope.
- ConcreteMQ must still be dependable for researchers as an experiment runner:
  repeated JSON tests and leader-owned experiment reporting are required before
  calling the current MQ version workable.
- Any difference from Abstract QUANTAS must be documented and validated with
  evidence.

## Current Baseline

Already working:
- [x] JSON parsing exists in both `ConcreteMqLeader` and `ConcreteMqPeer`.
- [x] `make mq_run_all` launches one leader process plus N peer processes.
- [x] `ProcessCoordinatorMQ` supports startup readiness, assignment delivery,
  start, done, stop, and cleanup.
- [x] `MqTopology::buildTopology(...)` computes neighbor assignments.
- [x] Each peer process owns one inbox queue named `peer_<id>`.
- [x] `NetworkInterfaceConcreteMQ` checks neighbor sets before send.
- [x] `Packet` crosses the process boundary through Boost serialization.
- [x] Peer receive/compute execution is process-local.
- [x] Abstract `BS::thread_pool` is not used for MQ peer execution.
- [x] Peer outputs are disambiguated per process.
- [ ] JSON `tests > 1` is executed by MQ instead of warning/skipping repeated
  computations.
- [ ] The leader writes a researcher-facing experiment report; per-peer files
  remain debugging artifacts.

Baseline validation command:

```sh
make mq_run_all INPUTFILE=... MQ_TOTAL_PEERS=<N> [MQ_ROUNDS=<R>]
```

## 1. What Is ConcreteMQ Supposed To Be?

Decision:
- ConcreteMQ will be a realistic process/network backend like TCP.
- It will not try to reproduce strict Abstract QUANTAS lockstep semantics in the
  current MQ version.

Main question:
- Which Abstract behaviors still need to be documented, measured, or selectively
  rebuilt inside this realistic backend?

Why this matters:
- Abstract QUANTAS is a lockstep simulator.
- ConcreteMQ currently has independent peer processes and no global per-round
  barrier.
- This decision controls whether MQ should accept real OS/process timing or add
  strict Abstract-style synchronization.

Confirmed answer:
- Keep ConcreteMQ as a realistic process backend for now.
- Do not add a global per-round IPC barrier unless a specific validation need
  proves strict Abstract parity is required.

This answer decides:
- whether independent peer progress is acceptable;
- whether `RoundManager`-dependent algorithms need special comparison;
- whether a future round barrier is necessary.

## 2. Should ConcreteMQ Rebuild Abstract Channel Behavior?

Decision:
- ConcreteMQ will behave as a real IPC transport backend.
- It will not rebuild Abstract `Channel` semantics such as configured delay,
  model drop, duplicate, reorder, `maxMsgsRec`, or channel size in the current
  MQ version.
- Those Abstract channel rules remain simulator-only behavior unless a future
  research need explicitly reopens this decision.

Main question:
- How should the real MQ transport behavior be documented and observed so that
  it is not confused with Abstract simulator channel behavior?

Abstract channel semantics:
- delay;
- drop;
- duplicate;
- reorder;
- `maxMsgsRec`;
- size.

Why this matters:
- TCP behaves like a real transport backend, not like the Abstract `Channel`.
- MQ has real OS/runtime effects such as FIFO queueing, queue capacity,
  scheduling delay, and backpressure.
- Those real effects are not the same as experiment-controlled Abstract channel
  semantics.

Confirmed answer:
- Keep one MQ inbox per peer.
- Treat Boost/POSIX MQ behavior as real IPC transport behavior.
- Do not implement Abstract channel semantics in `NetworkInterfaceConcreteMQ` for
  the current MQ version.
- Document any behavior difference from Abstract QUANTAS with validation
  evidence.

This answer decides:
- whether a pending-delivery buffer becomes only a structure seam or a real
  delivery-decision layer;
- whether `dropProbability` remains Abstract-only behavior in this backend;
- whether delay/reorder/`maxMsgsRec` stay out of the current MQ scope;
- whether packet metadata should focus on real IPC observation such as
  wall-clock timestamps instead of simulated channel delivery metadata.

## 3. What Should Message Delivery Mean In MQ?

Decision:
- Keep MQ delivery behavior simple for the current version: a received packet is
  deserialized and made visible to the algorithm in `_inStream`.
- Do not add a pending-delivery buffer unless it is needed for clean
  instrumentation. Since MQ is not rebuilding Abstract channel rules, the buffer
  is not required for the fast dependable version.

Main question:
- What minimal receive/send observations are needed to trust real IPC delivery?

Current MQ path:

```text
peer_<id> inbox
  -> raw bytes
  -> Packet
  -> _inStream
```

Why this matters:
- It keeps the IPC path easy to reason about.
- It avoids adding simulator-parity structure that is not needed for ZeroMQ.
- It still needs enough counters/logs to make the backend trustworthy.

Confirmed answer:
- Keep direct delivery to `_inStream` for now.
- Track receive counters such as `received_raw` and `delivered_to_instream`.
- Track send/backpressure counters such as `sent` and `dropped_backpressure`.
- Do not claim Abstract channel parity from these counters.

Related drop-counting decision:
- Do not use one generic `drops` counter.
- Since MQ is real IPC transport only, count OS/MQ backpressure separately and
  keep model loss out of the current MQ behavior:

```text
sent
received_raw
delivered_to_instream
dropped_backpressure
```

This answer decides:
- how `NetworkInterfaceConcreteMQ::receive()` should be structured;
- whether current `timed_send(...)` failures should be renamed to
  `dropped_backpressure`;
- that `dropped_model` should not be reported as active MQ behavior in the
  current version;
- where real transport receive/send observations should be recorded.

## 4. What Experiment Output Does ConcreteMQ Need?

Main question:
- What output, logs, or experiment artifacts are needed before researchers can
  trust ConcreteMQ for algorithm experiments?

Why this matters:
- Peer processes currently write local output files.
- The leader waits for done messages but does not assemble one final experiment
  report.
- Abstract QUANTAS supports `tests > 1`; ConcreteMQ must support repeated
  computations to be dependable as an experiment runner.
- Process launching is intentionally handled by `make mq_run_all` for the current
  MQ version.

Confirmed answer:
- Keep predictable per-peer output files as local debugging artifacts.
- Make the MQ leader the researcher-facing experiment logger/aggregator process.
- Implement repeated JSON tests for the current MQ version.
- Include experiment index and test index in MQ output/report naming.
- Keep `make mq_run_all` as the smoke path while finishing the current MQ
  version.
- Defer any C++ process manager until after the makefile-orchestrated MQ backend
  is stable and validated.

This answer decides:
- whether per-peer files are only temporary/local artifacts;
- what data peers must send or write so the leader can produce one report;
- how Abstract-vs-MQ comparisons should be validated;
- how repeated tests reset peer state and output names.

## 5. What Evidence Proves ConcreteMQ Is Complete Enough?

Main question:
- What validation is enough to call the current MQ version workable and move on
  to ZeroMQ?

Recommended completion standard:
- `ExamplePeer` topology run passes.
- `Bitcoin3PeerMQDemo` passes with real IPC message traffic.
- `AltBitUtility` passes with repeated JSON tests enabled.
- Leader and all peer processes exit with code `0`.
- Logs show topology assignment, latency, send/receive counters, and
  backpressure drops if any occur.
- The leader writes a final experiment/test report.
- Per-peer debug output files are disambiguated.
- Docs state non-goals clearly: no Abstract channel semantics, no global
  lockstep barrier, no C++ process manager in the current version.

This answer decides:
- when ConcreteMQ is dependable enough for researchers to run algorithms over a
  real IPC transport layer;
- when it is reasonable to stop MQ work and start the ZeroMQ backend.

## Suggested Meeting Order

1. Decide whether ConcreteMQ is a realistic backend or an Abstract-parity
   backend.
2. Decide whether MQ should rebuild Abstract channel behavior.
3. Confirm the minimal direct-delivery counter design.
4. Decide the leader report and repeated-test contract.
5. Decide what evidence is enough to call MQ complete enough for ZeroMQ.

## One-Sentence Summary For The Meeting

ConcreteMQ will remain a realistic process backend like TCP, but it must still be
a dependable experiment runner: finish repeated tests, leader-owned reporting,
real IPC counters, and validation evidence before moving to ZeroMQ.
