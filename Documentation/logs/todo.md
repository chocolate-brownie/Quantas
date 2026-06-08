# TODO moved

The MQ parity master backlog has been moved to:

- `Documentation/mq-parity/00-master-index.md`

Start there and follow the indexed files.

## Future Runtime Architecture Note

Current MQ execution is a process-based runner:
- the makefile/shell launches one leader process plus N peer processes;
- `ConcreteMqLeader` and `ProcessCoordinatorMQ` coordinate rendezvous, assignments, start, stop, and cleanup;
- peer processes execute their local round loop and communicate through Boost/POSIX message queues.

Future work: evolve this into an integrated process-managed simulation runtime.
That means moving process lifecycle ownership into a C++ process manager that can:
- read the input configuration and determine the peer process set;
- spawn and track leader/peer child processes;
- enforce the one-simulated-peer-to-one-OS-process invariant when required;
- capture process exit codes, logs, and output artifacts;
- clean up stale MQ resources consistently;
- handle startup failure, peer crash, timeout, and teardown paths deterministically.

This is separate from M2 distribution/channel parity. M2 should first preserve Abstract
channel semantics over the existing process-based runner before replacing the runner with
a centralized process manager.

## Simulation Component TODOs for N-Process MQ Runtime

Assumption:
- the current MQ direction is a realistic process-based backend, with Abstract
  QUANTAS kept as the reference model rather than fully preserved lockstep
  semantics.

Current state:
- initialization is partially changed: the makefile launches one leader process
  plus N peer processes, and the leader coordinates readiness, topology
  assignments, start, done, and stop;
- execution is changed: MQ peer execution does not use the Abstract
  `BS::thread_pool`; parallelism comes from independent OS processes scheduled
  by Linux;
- the main round loop is process-local: each peer receives, computes, and sends
  through MQ independently, with no global per-round barrier;
- finalization/logging is partially changed: peer processes write
  disambiguated outputs, but a full central aggregation contract is not
  finalized.

Remaining TODOs:
- finalize the Simulation-component architecture note:
  - clearly state that current MQ is a process-based backend, not strict
    Abstract lockstep parity;
  - verify by keeping `Documentation/logs/arch.md` aligned with this decision.
- define the output aggregation contract:
  - decide whether final results are per-peer files, leader-aggregated output,
    or a separate ResultAggregator output;
  - verify by replacing the TODO output-artifact row in
    `Documentation/mq-parity/06-validation-matrix.md` with concrete evidence.
- keep the per-round barrier as an explicit future decision:
  - if strict Abstract parity is required, add a round barrier;
  - if realistic process behavior is intended, measure and document round drift
    instead of hiding it;
  - verify with a small experiment that records peer loop/round progress.
- eventually move process launching out of the makefile:
  - introduce a C++ process manager only when needed;
  - it should spawn peers, track PIDs, capture exit codes, clean MQ resources,
    and handle startup/crash/teardown failures;
  - verify by running the same scenario currently covered by `make mq_run_all`.
- finish M2 distribution/channel parity on top of the current process-based
  runner:
  - model delay, drop, duplicate, reorder, `maxMsgsRec`, and queue-size
    behavior;
  - verify by moving the Distribution parity row in
    `Documentation/mq-parity/06-validation-matrix.md` from TODO to PASS.
- implement `tests > 1` semantics:
  - support repeated test execution and structured per-test reporting in MQ;
  - verify by moving the `tests > 1` row in
    `Documentation/mq-parity/06-validation-matrix.md` from TODO to PASS.
- finalize stop policy:
  - remove fallback ambiguity around fixed rounds vs done signals;
  - verify deterministic stop behavior with stable logs and clean process
    exits.

## Network Component TODOs for N-Process MQ Runtime

Assumption:
- the Abstract `Network` component is being split across MQ-specific topology,
  assignment, peer-interface, and future channel-semantics responsibilities.

Current state:
- topology determination is mostly replaced:
  - `ConcreteMqLeader` reads experiment topology;
  - `MqTopology::buildTopology(...)` computes peer neighbor assignments;
  - `ProcessCoordinatorMQ::sendAssignments(...)` sends assignments to peer
    processes;
  - `ConcreteMqPeer` builds local peers from assignments and installs
    `NetworkInterfaceConcreteMQ`;
  - M1 topology parity is recorded as PASS for complete, ring, grid, and
    userList scenarios in `Documentation/mq-parity/06-validation-matrix.md`.
- channel object creation is intentionally replaced differently:
  - Abstract QUANTAS creates in-memory `Channel` objects per edge;
  - ConcreteMQ creates one named inbox queue per peer and enforces topology
    through neighbor filtering before send;
  - this avoids one OS queue per topology edge, but means channel behavior must
    be represented in MQ runtime logic.
- IPC message delivery is replaced:
  - `NetworkInterfaceConcreteMQ::unicastTo(...)` serializes packets and sends
    bytes to `peer_<dest>` queues;
  - `NetworkInterfaceConcreteMQ::receive(...)` drains the local inbox and
    reconstructs `Packet` objects.
- phase participation is process-local:
  - there is no single global `Network` object applying receive/compute phases
    across all peers;
  - each peer process calls receive/compute/end-of-round locally.

Remaining TODOs:
- design the MQ channel-semantics layer:
  - decide where delay, drop, duplicate, reorder, `maxMsgsRec`, and queue-size
    behavior live in the MQ runtime;
  - verify by documenting the chosen mapping before runtime edits.
- finish M2 distribution/channel parity:
  - implement configured delay models (`UNIFORM`, `POISSON`, `ONE`);
  - implement configured `dropProbability`, `duplicateProbability`, and
    `reorderProbability`;
  - implement or explicitly model `maxMsgsRec` and `size` behavior;
  - verify with scenario-specific inputs and evidence in
    `Documentation/mq-parity/06-validation-matrix.md`.
- preserve the per-peer inbox tradeoff:
  - keep one inbox queue per peer unless a specific M2 requirement proves
    per-edge queues are necessary;
  - verify that dense topologies do not require queue counts proportional to
    edge count.
- define observability for network semantics:
  - add clear evidence for when packets are dropped, delayed, duplicated,
    reordered, or capped by receive limits;
  - verify using small controlled ExamplePeer scenarios before larger algorithms.
- clarify phase semantics for MQ:
  - keep process-local receive/compute behavior if realistic process execution
    remains the target;
  - only introduce a global phase/barrier protocol if strict Abstract parity is
    selected later;
  - verify by documenting round/phase behavior alongside any M2 validation logs.

## Abstract Node / Peer TODOs for N-Process MQ Runtime

Assumption:
- concrete algorithm code should remain as backend-agnostic as possible.
  Algorithm implementations such as Bitcoin, PBFT, Raft, and ExamplePeer should
  not need to know whether messages move through Abstract channels or Boost/POSIX
  message queues.

Current state:
- the core computation hook is preserved:
  - concrete peers still inherit from `Peer`;
  - algorithms still implement `performComputation()`;
  - MQ peer processes call `tryPerformComputation()`.
- the high-level user messaging API is mostly preserved:
  - `broadcast(...)`, `unicastTo(...)`, `multicast(...)`, `receive()`, and
    `popInStream()` still exist on `Peer`;
  - MQ swaps the peer's network interface to `NetworkInterfaceConcreteMQ`.
- runtime ownership has changed:
  - in Abstract mode, all peer objects live inside one process and can be passed
    as a full `std::vector<Peer*>`;
  - in MQ mode, each process owns only its local peer object(s).
- global peer-list hooks are not fully clarified:
  - `initParameters(std::vector<Peer*>...)`;
  - `endOfRound(std::vector<Peer*>...)`;
  - `endOfExperiment(std::vector<Peer*>...)`.

Remaining TODOs:
- document the Peer contract in MQ mode:
  - classify APIs as local-safe, backend-routed, or global-peer-list risky;
  - verify with an architecture note before changing code.
- audit concrete algorithms for global peer-list dependency:
  - inspect `initParameters(...)`, `endOfRound(...)`, `endOfExperiment(...)`,
    loops over `std::vector<Peer*>`, and direct reads of other peer state;
  - verify with a table: algorithm, uses full peer vector, use case, MQ risk.
- classify hook behavior in MQ mode:
  - decide whether each hook is local-only, requires aggregation, or is not yet
    supported;
  - verify by recording the classification in documentation.
- define the minimum state-sharing contract:
  - avoid serializing full `Peer` objects unless evidence proves it necessary;
  - prefer explicit snapshots for peer id, round, local metrics, counters, and
    final output values;
  - verify with a draft schema or Markdown table before implementation.
- decide how global hooks should work:
  - option A: keep hooks local-only in MQ mode;
  - option B: aggregate metrics/snapshots without reconstructing full peers;
  - option C: introduce an explicit snapshot API for algorithms that need global
    state;
  - recommended starting point is option B, pending algorithm audit evidence.
- add a small validation scenario:
  - prove that `performComputation()`, `broadcast(...)`, `popInStream()`, and
    local `endOfRound(...)` execute correctly in MQ mode;
  - verify with a controlled ExamplePeer-style run and hook evidence logs.
- update the MQ validation matrix:
  - add or refine a row for Peer API compatibility / Abstract Node contract;
  - expected result should separate working local computation/messaging APIs
    from pending global peer-vector semantics.

## Node Network Interface TODOs for N-Process MQ Runtime

Assumption:
- `NetworkInterfaceConcreteMQ` should hide Boost/POSIX message queue details
  from algorithm code while preserving the high-level `NetworkInterface` API.
  Boost MQ is the raw process transport; the node network interface is the
  QUANTAS semantics layer.

Current state:
- identity and topology filtering are implemented:
  - `configure(id, neighbors)` stores the peer id and neighbor set;
  - sends to non-neighbors are ignored before opening a destination queue.
- per-peer inbox transport is implemented:
  - each peer opens its own inbox queue, `peer_<id>`;
  - sends open the destination queue, `peer_<dest>`.
- serialization is implemented:
  - `unicastTo(...)` wraps a JSON message in `Packet`;
  - `Packet` is serialized with Boost `binary_oarchive`;
  - `receive()` deserializes raw bytes with Boost `binary_iarchive`.
- basic send path is implemented:
  - `unicastTo(...)` sends serialized bytes with `message_queue::timed_send(...)`;
  - timed-send backpressure drops are counted per destination.
- basic receive path is implemented:
  - `receive()` drains all currently available messages with `try_receive(...)`;
  - deserialized packets are pushed directly into `_inStream`;
  - latency is printed from packet send timestamp to receive time.
- base API reuse is preserved:
  - `broadcast(...)`, `multicast(...)`, `broadcastBut(...)`, and
    `randomMulticast(...)` still use the shared `NetworkInterface` logic and
    route through `unicastTo(...)`.

Remaining TODOs:
- add a pending-delivery buffer:
  - raw MQ messages should not always become algorithm-ready immediately;
  - use a local buffer between MQ inbox reads and `_inStream`;
  - verify with a small scenario where received raw packets are not delivered
    until their configured readiness condition is met.
- separate model drops from backpressure drops:
  - configured `dropProbability` is experiment semantics;
  - timed-send failure is runtime backpressure/liveness protection;
  - track separate counters such as `dropped_model` and
    `dropped_backpressure`.
- implement send-side channel semantics:
  - neighbor check is done;
  - add configured drop, configured duplicate, and delivery metadata assignment;
  - verify one semantic at a time, starting with the smallest controlled case.
- implement receive/pending-buffer channel semantics:
  - delay readiness;
  - reorder behavior;
  - `maxMsgsRec` delivery cap;
  - queue-size behavior if not handled entirely on send;
  - verify with scenario-specific ExamplePeer inputs before larger algorithms.
- define per-link vs per-peer configuration scope:
  - decide whether channel semantics are keyed by source/destination pair,
    destination peer, or global experiment distribution;
  - verify by documenting the keying rule before coding M2.
- improve observability counters:
  - track sent, received_raw, delivered_to_instream, duplicated, delayed,
    dropped_model, dropped_backpressure, and latency;
  - verify that validation logs can explain why each packet was delivered,
    delayed, duplicated, or dropped.
- keep implementation incremental:
  - do not implement all channel semantics at once;
  - first target should be one small M2 behavior, with a validation-matrix row
    and reproducible command evidence.

## Packet Component TODOs for N-Process MQ Runtime

Assumption:
- in the N-process MQ backend, `Packet` should be treated as a process-safe
  transport envelope. It must preserve the user payload and routing metadata
  while carrying enough timing/delivery metadata for the selected MQ semantics.

Current state:
- source and destination are preserved:
  - `Packet` stores source and target peer ids;
  - `NetworkInterfaceConcreteMQ::unicastTo(...)` sets both before sending.
- payload is preserved:
  - the user-defined message body is stored as JSON;
  - MQ serialization writes the JSON body as a string and parses it on receive.
- process-boundary serialization is implemented:
  - `Packet` supports Boost serialization;
  - MQ send converts `Packet` to bytes with `binary_oarchive`;
  - MQ receive reconstructs `Packet` with `binary_iarchive`.
- wall-clock send timestamp is implemented:
  - MQ sets a send timestamp before serialization;
  - receive-side code uses it for latency observation.
- Abstract delay/round metadata is not fully carried through MQ:
  - `_delay` and `_round` still exist in `Packet`;
  - current MQ serialization does not save/load them;
  - this is acceptable for the current process-backend direction, but must be
    decided explicitly before M2 delay semantics.

Remaining TODOs:
- define the MQ Packet metadata contract:
  - keep source id, destination id, payload, serialization format, and send
    timestamp as stable fields;
  - replace the narrow "packet delay" concept with broader delivery metadata;
  - verify by documenting which fields are serialized and why.
- decide what delivery metadata means:
  - strict Abstract parity: serialize logical send round and delay, then deliver
    when the receiver's logical round reaches eligibility;
  - realistic process backend: use wall-clock send/delivery timing and treat
    round delay as optional or derived;
  - hybrid: carry both logical and wall-clock metadata, but only if validation
    needs both.
- decide whether `_delay` and `_round` should be serialized:
  - do not add them blindly;
  - first identify which M2 behavior requires them;
  - verify with one controlled scenario before changing packet format broadly.
- define delivery eligibility:
  - specify how `NetworkInterfaceConcreteMQ` decides when a received packet
    enters `_inStream`;
  - this may use logical round, wall-clock delivery time, pending-buffer order,
    or explicit channel metadata.
- keep packet format changes minimal:
  - avoid serializing full peer or algorithm state;
  - avoid adding fields without a validation use case;
  - verify compatibility with existing MQ smoke runs after any packet-format
    change.
