# QUANTAS Abstract to N-Process MQ Architecture

This note is the working architecture map for moving original Abstract QUANTAS
from one process with in-memory peers/channels to a one-machine, N-process
Boost/POSIX message-queue backend.

Working assumption:
- ConcreteMQ is currently a realistic process-based backend, not strict
  Abstract lockstep parity;
- Abstract QUANTAS remains the reference model for terminology and behavioral
  gaps;
- ConcreteMQ still needs researcher-grade experiment behavior: repeated JSON
  tests and leader-owned experiment reporting are part of the current workable
  MQ scope;
- process orchestration remains makefile-based for now; a C++ process manager is
  deferred until after the current MQ backend is stable;
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
  per-peer debug output plus leader-owned experiment report target
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
- run each JSON test as a fresh coordinated computation;
- act as the experiment-level logger/aggregator by gathering process-local
  outputs after peers finish.

Current code state:
- `make mq_run_all` launches the current process-based runner;
- `ConcreteMqLeader` coordinates readiness, topology assignment, start, done,
  stop, and cleanup through `ProcessCoordinatorMQ`;
- `ConcreteMqPeer` owns the local peer round loop;
- MQ peer execution does not use Abstract `BS::thread_pool`;
- each peer writes a disambiguated output file;
- repeated JSON tests and leader-owned experiment reporting are still required
  before calling the current MQ version researcher-ready.

Current Simulation shape:

```text
makefile
  starts leader process
  starts N peer processes

leader
  parse config
  for each experiment
    for each configured test
      create barrier/done queues
      wait for peer ready messages
      compute topology assignments
      send assignments
      broadcast start
      wait for done messages
      gather peer completion, counters, and output references
      write/update one experiment-level report
      broadcast stop / cleanup

peer process
  parse config
  for each experiment
    for each configured test
      create peer_<id> inbox
      send ready
      receive assignment
      build local Peer through PeerRegistry
      install NetworkInterfaceConcreteMQ
      wait for start
      run local receive/compute/end-of-round loop
      write disambiguated local debug output
      notify done
```

Open decisions:
- what exact metric schema peers provide to the leader so it can produce one
  experiment-level report;
- what minimal leader report is enough for researchers to trust MQ experiment
  runs;
- how much algorithm-level metric aggregation is required before moving to
  ZeroMQ.

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
- observe real IPC behavior without rebuilding Abstract channel semantics.

Current code state:
- `ConcreteMqLeader` reads topology from the experiment config;
- `MqTopology::buildTopology(...)` computes `MqAssignment` values;
- `ProcessCoordinatorMQ::sendAssignments(...)` serializes assignments to peer
  inbox queues;
- `ConcreteMqPeer` receives its assignment, creates local peers, and installs
  `NetworkInterfaceConcreteMQ`;
- topology parity is implemented for the supported `MqTopology` cases already
  covered by the MQ parity work;
- Abstract distribution/channel semantics are intentionally not implemented in
  the current MQ backend.

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
one OS inbox no longer represents one Abstract `Channel`; this is acceptable
because ConcreteMQ is a real IPC backend, not an Abstract channel model clone.

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
- deliver received packets directly into `_inStream` for the current version;
- measure real IPC observations such as sent packets, received packets,
  delivered packets, latency, and backpressure drops;
- expose observability counters for validation.

Current receive path:

```text
peer_<id> inbox
  -> try_receive raw bytes
  -> Boost deserialize Packet
  -> push directly into _inStream
```

Current target receive path:

```text
peer_<id> inbox
  -> try_receive raw bytes
  -> Boost deserialize Packet
  -> _inStream
  -> increment received_raw / delivered_to_instream counters
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
  -> create/serialize Packet
  -> timed_send bytes to peer_<dest>
  -> record sent/dropped_backpressure counters
```

Current code state:
- identity and neighbor filtering are implemented;
- one named inbox queue per peer is implemented;
- `unicastTo(...)` serializes packets and sends with
  `message_queue::timed_send(...)`;
- `receive()` deserializes packets and pushes them directly into `_inStream`;
- base `broadcast(...)`, `multicast(...)`, `broadcastBut(...)`, and
  `randomMulticast(...)` still route through virtual `unicastTo(...)`;
- backpressure drops are counted internally, but not yet reported as a structured
  MQ statistic;
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
- carry real IPC observation metadata such as wall-clock send time.

Current code state:
- source and destination are preserved;
- JSON payload is serialized with `dump()` and parsed on receive;
- Boost serialization is implemented for MQ transport;
- wall-clock send timestamp exists and is used for latency observation;
- `_delay` and `_round` exist in `Packet`, but are not serialized by the MQ
  packet format because Abstract channel semantics are out of current MQ scope;
- `receive()` does not call `hasArrived()` before pushing to `_inStream`.

Open packet contract:

```text
Stable fields:
  source peer id
  destination peer id
  user payload
  serialization format
  send timestamp

Current MQ observation metadata:
  wall-clock send timestamp
  optional receive timestamp / latency summary
  optional per-peer send/receive/backpressure counters
```

Strict Abstract parity would require logical round and delay metadata, but that
is not part of the current ConcreteMQ scope.

## Architectural Invariants

Keep these constraints stable while implementing the backlog:

- algorithm code should remain mostly backend-agnostic;
- Boost MQ should be treated as raw IPC transport, not as the full QUANTAS
  network model;
- Abstract channel semantics stay in the Abstract simulator for the current MQ
  version;
- OS/MQ backpressure drops must be visible as real transport behavior;
- repeated JSON tests and leader-owned experiment reporting are required for a
  researcher-ready MQ version;
- peer/process-local state must not be confused with global experiment state;
- full `Peer` objects should not be serialized between processes;
- process lifecycle management stays makefile-orchestrated for now.
