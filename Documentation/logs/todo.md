# ConcreteMQ N-Process Runtime TODOs

The MQ parity master backlog lives at:

- `Documentation/mq-parity/00-master-index.md`

This file is the architecture-driven work queue for the N-process ConcreteMQ
runtime. It is ordered so each item can be attacked independently without
mixing process management, channel semantics, peer lifecycle hooks, packet
format, and aggregation in one large change.

Current working assumption:
- ConcreteMQ is a realistic process-based backend today;
- strict Abstract lockstep parity is not currently implemented;
- any semantic difference from Abstract QUANTAS must be documented and
  validated with evidence.

Legend:
- Done: already present in code or documentation.
- Next: the next recommended implementation target.
- Later: important, but should wait until earlier dependencies are stable.
- Decision: choose and document the model before coding.

## 0. Baseline Already in Place

These are not the next attack targets unless they regress.

Done:
- JSON parsing exists in both `ConcreteMqLeader` and `ConcreteMqPeer`.
- `make mq_run_all` launches one leader process plus N peer processes.
- `ProcessCoordinatorMQ` supports startup readiness, assignment delivery,
  start, done, stop, and cleanup.
- `MqTopology::buildTopology(...)` computes neighbor assignments.
- each peer process owns one inbox queue named `peer_<id>`.
- `NetworkInterfaceConcreteMQ` checks neighbor sets before send.
- `Packet` crosses the process boundary through Boost serialization.
- peer receive/compute execution is process-local.
- Abstract `BS::thread_pool` is not used for MQ peer execution.
- peer outputs are disambiguated per process.

Keep validating with:
- `make mq_run_all INPUTFILE=... MQ_TOTAL_PEERS=<N> [MQ_ROUNDS=<R>]`
- topology evidence in logs for complete, ring, grid, and userList scenarios.

## 1. Decide the ConcreteMQ Execution Model

Problem:
- The current MQ backend behaves like a realistic process runtime with no global
  per-round barrier.
- Abstract QUANTAS is a lockstep simulator.
- Future implementation choices depend on whether MQ should preserve lockstep
  parity or intentionally study process/network timing drift.

Recommendation:
- Keep ConcreteMQ as a realistic process backend for now.
- Do not add a per-round IPC barrier until a validation need proves strict
  Abstract parity is required.

Tasks:
- Decision: record ConcreteMQ as "realistic process backend, no global round
  barrier" in the validation docs.
- Next: add a small round-progress evidence run that logs each peer's local
  loop count/current round view.
- Later: if strict parity becomes necessary, design a separate IPC round barrier
  rather than mixing it into message delivery.

Done when:
- `Documentation/logs/arch.md` and the MQ validation matrix both state the same
  execution model;
- a reproducible MQ run shows independent peer loop progress;
- algorithms that depend on `RoundManager::currentRound()` are flagged for
  careful comparison.

## 2. Define the MQ Channel-Semantics Contract

Problem:
- ConcreteMQ currently provides raw process transport:

```text
neighbor check -> Packet -> serialize -> timed_send(peer_<dest>)
peer_<id> inbox -> deserialize -> _inStream
```

- Abstract QUANTAS also models channel behavior:

```text
delay
drop
duplicate
reorder
maxMsgsRec
size
```

- These semantics are not yet implemented in the MQ layer.

Recommendation:
- Keep one MQ inbox per peer.
- Rebuild Abstract channel behavior in `NetworkInterfaceConcreteMQ`, not by
  creating one OS queue per topology edge.
- Add semantics incrementally, one behavior at a time.

Tasks:
- Decision: define whether channel configuration is keyed globally, per
  destination peer, or per source/destination link.
- Next: document the exact first M2 semantic to implement.
- Next: choose the smallest controlled ExamplePeer input that proves the first
  semantic.
- Later: move each implemented behavior into
  `Documentation/mq-parity/06-validation-matrix.md` with command evidence.

Done when:
- the keying rule is written down before code changes;
- one M2 semantic has a reproducible input and expected output;
- validation logs explain why a packet was delivered, delayed, dropped,
  duplicated, reordered, or capped.

## 3. Add a Pending-Delivery Buffer

Problem:
- Current `receive()` pushes every deserialized MQ packet directly into
  `_inStream`.
- That makes "received from MQ" equal "algorithm-visible now."
- Abstract QUANTAS has a delivery decision between transport arrival and
  algorithm visibility.

Target shape:

```text
peer_<id> inbox
  -> raw bytes
  -> Packet
  -> pending-delivery buffer
  -> delivery eligibility
  -> _inStream
```

Tasks:
- Next: add a local pending packet buffer to `NetworkInterfaceConcreteMQ`.
- Next: keep initial behavior equivalent by immediately flushing all pending
  packets into `_inStream`.
- Next: add counters for `received_raw` and `delivered_to_instream`.
- Later: plug delay, reorder, and `maxMsgsRec` into the buffer.

Done when:
- existing MQ smoke runs still behave the same;
- receive logs or counters distinguish raw receipt from algorithm delivery;
- there is a clean insertion point for delay/reorder/receive-cap semantics.

## 4. Separate Model Drops from Backpressure Drops

Problem:
- Current `timed_send(...)` failures are counted as drops.
- These are OS/MQ backpressure symptoms, not simulated network loss.
- Abstract `dropProbability` represents model behavior.

Target counters:

```text
sent
dropped_model
dropped_backpressure
```

Tasks:
- Next: introduce structured counters in `NetworkInterfaceConcreteMQ`.
- Next: rename/record timed-send failures as `dropped_backpressure`.
- Later: implement configured `dropProbability` as `dropped_model`.
- Later: write counters to peer output or validation logs.

Done when:
- no validation output confuses model loss with queue-capacity loss;
- backpressure drops and configured drops are independently visible;
- a test or controlled run can force at least one backpressure drop without
  calling it a model drop.

## 5. Implement One Channel Semantic at a Time

Problem:
- Implementing delay, drop, duplicate, reorder, `maxMsgsRec`, and size in one
  patch would hide bugs and make validation weak.

Recommended order:
1. model drop;
2. duplicate;
3. receive cap / `maxMsgsRec`;
4. delay readiness;
5. reorder;
6. queue-size semantics, if Abstract parity requires more than OS MQ capacity.

Tasks for each semantic:
- define the exact input configuration field being honored;
- implement only that behavior;
- add a small controlled input;
- capture command output/log evidence;
- update the validation matrix row.

Done when:
- each semantic can be explained from one small run;
- each semantic has separate counters or logs;
- M2 distribution/channel parity can be marked PASS behavior by behavior.

## 6. Define the Packet Metadata Contract

Problem:
- `Packet` currently preserves source, destination, payload, and send timestamp
  across MQ.
- `_delay` and `_round` still exist in `Packet`, but are not serialized by the
  MQ packet format.
- Delivery semantics need a stable metadata contract before broad channel work.

Decision options:
- Strict Abstract parity: serialize logical send round and delay.
- Realistic process backend: use wall-clock delivery eligibility and local
  receive polling.
- Hybrid: carry both logical and wall-clock metadata only if validation needs
  both.

Recommendation:
- Do not serialize `_delay` and `_round` blindly.
- First choose the first channel semantic and identify what metadata it needs.

Tasks:
- Decision: document stable serialized fields and delivery metadata fields.
- Next: decide whether the first M2 semantic requires packet-format changes.
- Later: if packet format changes, clean rebuild before debugging behavior.

Done when:
- `Packet` field purpose is documented;
- MQ serialization format is documented;
- any added field has a validation use case;
- existing MQ smoke runs pass after packet-format changes.

## 7. Audit Peer Lifecycle Hooks

Problem:
- Algorithm-facing computation and messaging APIs mostly survive MQ unchanged.
- Lifecycle hooks are different:

```text
Abstract:
  initParameters/endOfRound/endOfExperiment receive all peers

ConcreteMQ today:
  hooks receive localPeers owned by this process
```

- This can silently break algorithms that inspect global peer state.

Tasks:
- Next: audit concrete algorithms for:
  - `initParameters(...)`;
  - `endOfRound(...)`;
  - `endOfExperiment(...)`;
  - loops over `std::vector<Peer*>`;
  - direct reads of other peer state.
- Next: create a table with:
  - algorithm;
  - hook used;
  - needs global peer vector;
  - MQ risk;
  - proposed contract.
- Decision: classify each hook as local-safe, aggregation-needed, or unsupported
  in MQ mode.

Done when:
- no algorithm hook ambiguity remains undocumented;
- global-peer-vector assumptions are visible before more MQ parity work;
- risky hooks have a proposed snapshot/aggregation strategy.

## 8. Define Output Aggregation

Problem:
- Peer processes write local output files.
- Leader waits for done messages.
- No central final experiment report is assembled yet.

Recommendation:
- Start with file-based aggregation after all peers finish.
- Avoid serializing full `Peer` objects.
- Aggregate explicit metric snapshots or per-peer JSON outputs.

Tasks:
- Decision: choose one output contract:
  - per-peer files only;
  - leader-aggregated report;
  - separate `ResultAggregator`.
- Next: define the final report schema.
- Next: update validation docs with expected output artifacts.
- Later: implement aggregation after MQ channel semantics are stable enough to
  produce meaningful metrics.

Done when:
- one completed MQ run produces predictable output artifact names;
- the leader/simulation responsibility for aggregation is explicit;
- `tests > 1` reporting has a place to live.

## 9. Implement `tests > 1` Semantics

Problem:
- Abstract QUANTAS supports repeated tests.
- ConcreteMQ currently warns that it executes a single test per experiment.

Tasks:
- Decision: decide whether repeated tests relaunch processes or reuse them
  across test iterations.
- Next: define output naming for experiment index, peer id, and test index.
- Later: implement repeated-test execution after output aggregation is defined.

Done when:
- `tests > 1` produces deterministic per-test outputs;
- repeated runs do not reuse stale MQ resources;
- validation matrix records the behavior as PASS.

## 10. Introduce a C++ Process Manager

Problem:
- Process launching is currently makefile/shell orchestration.
- This proves N independent processes, but it is not an integrated Simulation
  Component replacement.

Recommendation:
- Do not implement this before channel semantics and output contracts are
  clearer.
- Keep `make mq_run_all` as the smoke path until a process manager has a
  concrete acceptance test.

Tasks:
- Later: introduce a process manager that can:
  - parse config and determine peer count;
  - spawn leader/peer child processes;
  - track PIDs and exit codes;
  - capture logs/output paths;
  - handle startup failure, peer crash, timeout, and teardown;
  - clean stale MQ resources deterministically.
- Later: replace the makefile launcher only after matching its current behavior.

Done when:
- the process manager can run the same scenario as `make mq_run_all`;
- failed peer startup produces a clear failure path;
- all child exit codes are captured;
- stale MQ cleanup is deterministic.

## Suggested Attack Order

1. Decide/document ConcreteMQ execution model.
2. Define channel-semantics keying rule.
3. Add pending-delivery buffer with behavior preserved.
4. Add structured counters and separate backpressure drops.
5. Implement one model semantic, starting with configured drop.
6. Define packet metadata changes only when required by a semantic.
7. Audit peer lifecycle hooks.
8. Define output aggregation.
9. Implement `tests > 1`.
10. Replace makefile orchestration with a C++ process manager.

This order keeps each task small and verifiable. It also prevents a process
manager refactor from hiding the harder semantic work inside
`NetworkInterfaceConcreteMQ`.
