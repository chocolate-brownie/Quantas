# QUANTAS Abstract to N-Process MQ Architecture

This note is the working architecture map for moving original Abstract QUANTAS
from one process with in-memory peers/channels to a one-machine, N-process
Boost/POSIX message-queue backend.

Working assumption:
- ConcreteMQ is currently a realistic process-based backend, not strict
  Abstract lockstep parity;
- Abstract QUANTAS remains the reference model for terminology and behavioral
  gaps;
- differences from Abstract behavior should be explicit, measurable, and
  validated instead of hidden.

Core architecture shift:

```text
Abstract QUANTAS:
  one Linux process
  many Peer objects in one address space
  in-memory Channel objects
  thread-pool receive/compute phases
  one shared LogWriter view

ConcreteMQ:
  one leader process
  N peer processes
  one named MQ inbox per peer: peer_<id>
  OS process scheduling
  process-local receive/compute loops
  per-peer output today, central aggregation pending
```

## Simulation Component

Abstract role:
- parse JSON configuration;
- set tests, rounds, topology, delay models, and fault probabilities;
- build all peers and network state in one process;
- run lockstep receive, compute, send/end-of-round phases with a thread pool;
- finalize metrics through one shared `LogWriter`.

N-process MQ role:
- parse the same experiment configuration;
- coordinate one leader process plus N peer processes;
- create or clear MQ resources;
- distribute peer ids and topology assignments;
- start peers after all are ready;
- let Linux schedule peer processes independently;
- gather or aggregate process-local outputs after peers finish.

Current code state:
- `make mq_run_all` launches the current process-based runner;
- `ConcreteMqLeader` coordinates readiness, topology assignment, start, done,
  stop, and cleanup through `ProcessCoordinatorMQ`;
- `ConcreteMqPeer` owns the local peer round loop;
- MQ peer execution does not use Abstract `BS::thread_pool`;
- each peer writes a disambiguated output file;
- leader waits for completion signals, but does not aggregate metrics.

Current Simulation shape:

```text
makefile
  starts leader process
  starts N peer processes

leader
  parse config
  create barrier/done queues
  wait for peer ready messages
  compute topology assignments
  send assignments
  broadcast start
  wait for done messages
  broadcast stop / cleanup

peer process
  parse config
  create peer_<id> inbox
  send ready
  receive assignment
  build local Peer through PeerRegistry
  install NetworkInterfaceConcreteMQ
  wait for start
  run local receive/compute/end-of-round loop
  write local output
  notify done
```

Open decisions:
- whether ConcreteMQ should stay realistic process-backend execution or later
  add a per-round IPC barrier for strict Abstract parity;
- whether final outputs remain per-peer files or a leader/aggregator produces
  one experiment-level report;
- when to replace makefile orchestration with a C++ process manager.

## Network Component

Abstract role:
- build all peer objects in one memory space;
- compute topology from the configuration;
- create in-memory `Channel` objects between neighbors;
- pass delay, drop, duplicate, reorder, receive-cap, and queue-size settings
  into channels;
- expose receive/compute phase operations for `Simulation` to schedule.

N-process MQ role:
- compute topology centrally or deterministically;
- distribute each peer's neighbor assignment;
- replace in-memory edge channels with IPC message transport;
- use one inbox queue per peer instead of one queue per edge;
- enforce topology in the sender interface;
- rebuild Abstract channel semantics explicitly in the MQ runtime.

Current code state:
- `ConcreteMqLeader` reads topology from the experiment config;
- `MqTopology::buildTopology(...)` computes `MqAssignment` values;
- `ProcessCoordinatorMQ::sendAssignments(...)` serializes assignments to peer
  inbox queues;
- `ConcreteMqPeer` receives its assignment, creates local peers, and installs
  `NetworkInterfaceConcreteMQ`;
- topology parity is implemented for the supported `MqTopology` cases already
  covered by the MQ parity work;
- full distribution/channel semantics are not implemented yet.

Per-peer inbox tradeoff:

```text
Literal per-edge IPC model:
  queue_0_to_1
  queue_1_to_0
  queue_0_to_2
  ...

Current MQ model:
  peer_0
  peer_1
  peer_2
  ...
```

This keeps OS queue count linear in peer count. A complete 100-peer topology
needs 100 peer inboxes instead of 9900 directed edge queues. The cost is that
one OS inbox no longer represents one Abstract `Channel`; per-link behavior
must be represented in C++ metadata and delivery logic.

## Abstract Node / Peer Component

Abstract role:
- define the computation hook through `performComputation()`;
- provide the base class for user algorithms;
- expose algorithm-facing APIs such as `broadcast(...)`, `unicastTo(...)`,
  `receive()`, and `popInStream()`;
- allow lifecycle hooks such as `initParameters(...)`, `endOfRound(...)`, and
  `endOfExperiment(...)` to receive the full `std::vector<Peer*>` because all
  peers live in one process.

N-process MQ role:
- preserve the algorithm-facing `Peer` API where possible;
- construct algorithm peers through `PeerRegistry`;
- call `tryPerformComputation()` in each peer process;
- route messaging APIs through `NetworkInterfaceConcreteMQ`;
- define a new contract for hooks that previously relied on the full peer
  vector.

Current code state:
- algorithms still inherit from `Peer`;
- `performComputation()` remains the algorithm computation hook;
- `ConcreteMqPeer` calls `peer->tryPerformComputation()`;
- `PeerRegistry::makePeer(...)` constructs local algorithm peers;
- `setNetworkInterface(...)` installs `NetworkInterfaceConcreteMQ`;
- local hook wiring exists, but global peer-vector semantics are not solved.

Critical caveat:
- `performComputation()`, `broadcast(...)`, `unicastTo(...)`, `receive()`, and
  `popInStream()` are mostly safe in MQ mode;
- `initParameters(...)`, `endOfRound(...)`, and `endOfExperiment(...)` are risky
  if an algorithm expects the passed vector to contain every peer in the
  experiment;
- Abstract QUANTAS calls aggregate hooks through `_peers[0]` with the full peer
  vector, so MQ must not silently pretend a local-only vector is equivalent.

## Node Network Interface Component

Abstract role:
- own inbound/outbound channel references;
- wrap outgoing messages into `Packet` objects;
- place packets into channel tails;
- during `receive()`, inspect channels and move arrived packets into `_inStream`;
- rely on `Channel` for delay, drop, duplicate, reorder, `maxMsgsRec`, and size
  behavior.

N-process MQ role:
- hide Boost/POSIX MQ details from algorithm code;
- keep the shared `NetworkInterface` API;
- own/open this process's inbox queue `peer_<id>`;
- open destination inbox queues `peer_<dest>` on send;
- serialize and deserialize packets across process boundaries;
- maintain a pending-delivery buffer before packets reach `_inStream`;
- implement channel semantics above raw MQ transport;
- expose observability counters for validation.

Current receive path:

```text
peer_<id> inbox
  -> try_receive raw bytes
  -> Boost deserialize Packet
  -> push directly into _inStream
```

Target receive path:

```text
peer_<id> inbox
  -> try_receive raw bytes
  -> Boost deserialize Packet
  -> pending-delivery buffer
  -> delay/reorder/maxMsgsRec/delivery eligibility
  -> _inStream
```

Current send path:

```text
unicastTo(msg, dest)
  -> check dest is a neighbor
  -> create Packet(source, destination, payload, send timestamp)
  -> Boost serialize Packet to bytes
  -> timed_send bytes to peer_<dest>
```

Target send path:

```text
unicastTo(msg, dest)
  -> check dest is a neighbor
  -> apply configured drop/duplicate/delivery metadata
  -> create/serialize Packet
  -> timed_send bytes to peer_<dest>
  -> record sent/dropped_model/dropped_backpressure counters
```

Current code state:
- identity and neighbor filtering are implemented;
- one named inbox queue per peer is implemented;
- `unicastTo(...)` serializes packets and sends with
  `message_queue::timed_send(...)`;
- `receive()` deserializes packets and pushes them directly into `_inStream`;
- base `broadcast(...)`, `multicast(...)`, `broadcastBut(...)`, and
  `randomMulticast(...)` still route through virtual `unicastTo(...)`;
- backpressure drops are counted internally, but model drops are not separate;
- latency is printed, but not structured as a validation metric.

## Packet Component

Abstract role:
- carry source id;
- carry destination id;
- carry the user-defined payload;
- carry delay and sent-round metadata so channels can decide when the packet has
  arrived.

N-process MQ role:
- act as a process-safe transport envelope;
- preserve source, destination, and payload;
- serialize to bytes before entering MQ;
- deserialize back into a packet after leaving MQ;
- carry delivery metadata needed by the selected MQ semantics.

Current code state:
- source and destination are preserved;
- JSON payload is serialized with `dump()` and parsed on receive;
- Boost serialization is implemented for MQ transport;
- wall-clock send timestamp exists and is used for latency observation;
- `_delay` and `_round` exist in `Packet`, but are not currently serialized by
  the MQ packet format;
- `receive()` does not call `hasArrived()` before pushing to `_inStream`.

Open packet contract:

```text
Stable fields:
  source peer id
  destination peer id
  user payload
  serialization format
  send timestamp

Open delivery metadata:
  logical send round
  logical delay
  wall-clock delivery eligibility
  sequence number, if reorder validation needs it
```

Strict Abstract parity likely requires logical round and delay. A realistic
process backend may prefer wall-clock delivery metadata. A hybrid should only
carry both if validation needs both.

## Architectural Invariants

Keep these constraints stable while implementing the backlog:

- algorithm code should remain mostly backend-agnostic;
- Boost MQ should be treated as raw IPC transport, not as the full QUANTAS
  network model;
- `NetworkInterfaceConcreteMQ` is the place to rebuild QUANTAS channel
  semantics;
- model drops and OS/MQ backpressure drops must be separate concepts;
- peer/process-local state must not be confused with global experiment state;
- full `Peer` objects should not be serialized between processes;
- process lifecycle management is separate from channel-semantics parity.
