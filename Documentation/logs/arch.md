# QUANTAS Abstract-to-N-Process MQ Architecture Study

This note captures the working architecture model for moving original Abstract
QUANTAS from one-process simulation toward an N-process, one-machine
Boost/POSIX message-queue backend.

Current working assumption:
- the ConcreteMQ backend is closer to a realistic process-based communication
  backend than to strict Abstract lockstep parity;
- Abstract QUANTAS remains the reference model for terminology, component
  responsibilities, and validation gaps;
- differences from Abstract behavior must be documented and measured rather than
  hidden.

High-level model:

```text
Abstract QUANTAS:
  one process
  many peer objects
  in-memory channels
  thread-pool receive/compute phases
  one shared LogWriter view

ConcreteMQ direction:
  one leader process
  N peer processes
  one MQ inbox per peer
  OS process scheduling
  process-local receive/compute loops
  per-peer output now, aggregation contract pending
```

## Simulation Component

Abstract role:
- parse configuration;
- set rounds/tests;
- initialize topology and distribution settings;
- run receive/compute/end-of-round phases using a thread pool;
- finalize metrics through `LogWriter`.

N-process MQ role:
- parse the same experiment configuration;
- launch or coordinate one leader process plus N peer processes;
- create/clear MQ resources;
- distribute topology assignments and start/stop signals;
- let OS scheduling provide process-level parallelism;
- gather or aggregate process-local outputs after peers finish.

Current state:
- `make mq_run_all` is the practical process launcher;
- `ConcreteMqLeader` and `ProcessCoordinatorMQ` coordinate readiness,
  assignments, start, done, and stop;
- `ConcreteMqPeer` owns the local peer round loop;
- the Abstract `BS::thread_pool` execution path is not used for MQ peer
  execution;
- full central output aggregation is not finalized.

Open design point:
- strict Abstract parity would require a per-round IPC barrier;
- realistic process-backend behavior can intentionally keep independent peer
  progress and study the resulting timing/round drift.

## Network Component

Abstract role:
- build all peers in one memory space;
- compute topology from config;
- create in-memory `Channel` objects between neighbors;
- pass distribution settings into each channel;
- expose receive/compute/end-of-round operations for `Simulation` to schedule.

N-process MQ role:
- compute topology centrally or deterministically;
- distribute per-peer neighbor assignments;
- replace in-memory edge channels with process message transport;
- keep topology enforcement in the sender interface;
- rebuild channel/distribution semantics explicitly on top of MQ transport.

Current state:
- `ConcreteMqLeader` reads topology from the experiment config;
- `MqTopology::buildTopology(...)` computes neighbor assignments;
- `ProcessCoordinatorMQ::sendAssignments(...)` sends assignments to peers;
- `ConcreteMqPeer` creates local peer objects and installs
  `NetworkInterfaceConcreteMQ`;
- M1 topology parity is documented as PASS for complete, ring, grid, and
  userList;
- M2 distribution/channel semantics remain open.

## Abstract Node / Peer Component

Abstract role:
- define the algorithm computation hook through `performComputation()`;
- provide a stable base class for user algorithms;
- expose high-level messaging APIs such as `broadcast(...)`, `unicastTo(...)`,
  `receive()`, and `popInStream()`;
- allow hooks such as `initParameters(...)`, `endOfRound(...)`, and
  `endOfExperiment(...)` to receive a full `std::vector<Peer*>` because all
  peers are in one process.

N-process MQ role:
- preserve the algorithm-facing `Peer` API as much as possible;
- create one local peer object inside each peer process;
- swap the underlying network interface to `NetworkInterfaceConcreteMQ`;
- treat global peer-list hooks as a separate unresolved contract.

Current state:
- concrete peers still inherit from `Peer`;
- algorithms still implement `performComputation()`;
- MQ peer processes call `tryPerformComputation()`;
- `setNetworkInterface(...)` allows MQ to install `NetworkInterfaceConcreteMQ`;
- local hook wiring exists, but global peer-vector semantics are not fully
  solved.

Important caveat:
- `performComputation()` and messaging APIs are mostly safe in MQ mode;
- hooks that inspect all peers are risky because a peer process naturally owns
  only local peer state.

## Node Network Interface Component

Abstract role:
- own inbound/outbound channel references;
- wrap outgoing messages into `Packet` objects;
- place packets into channel tails;
- during `receive()`, inspect channels and move arrived packets into `_inStream`;
- rely on `Channel` for delay, drop, duplicate, reorder, receive cap, and queue
  size behavior.

N-process MQ role:
- hide Boost/POSIX MQ details from algorithm code;
- keep the shared `NetworkInterface` API;
- own the peer inbox queue `peer_<id>`;
- open destination inbox queues `peer_<dest>` on send;
- serialize/deserialize packets across process boundaries;
- eventually maintain a pending-delivery buffer and MQ-side channel semantics
  before packets reach `_inStream`.

Current state:
- identity and neighbor filtering are implemented;
- one named inbox queue per peer is implemented;
- `unicastTo(...)` serializes `Packet` to bytes and sends with
  `message_queue::timed_send(...)`;
- `receive()` drains raw MQ messages, deserializes packets, and pushes them
  directly into `_inStream`;
- `broadcast(...)`, `multicast(...)`, and related APIs still route through the
  shared base interface;
- backpressure drops are counted, but they are runtime liveness drops, not
  configured model drops.

Open work:
- add a pending-delivery buffer;
- separate model drops from backpressure drops;
- implement delay/drop/duplicate/reorder/`maxMsgsRec`/`size` semantics
  incrementally;
- improve observability counters for validation.

## Packet Component

Abstract role:
- carry source id;
- carry destination id;
- carry the user-defined message payload;
- carry delay and sent-round metadata so channels can decide when the packet
  has arrived.

N-process MQ role:
- become a process-safe transport envelope;
- preserve source, destination, and payload;
- support object-to-bytes-to-object serialization;
- carry wall-clock send timestamp for latency observation;
- define delivery metadata for the chosen MQ semantics.

Current state:
- source and destination are preserved;
- JSON payload is serialized as a string and parsed on receive;
- Boost serialization is implemented for process-boundary transport;
- wall-clock send timestamp exists and is used for latency observation;
- Abstract `_delay` and `_round` fields still exist but are not serialized by the
  current MQ packet format.

Open design point:
- strict Abstract parity may require serializing logical round and delay;
- realistic process-backend behavior may keep wall-clock delivery metadata as
  primary and treat round delay as optional;
- the packet metadata contract should be defined before broad M2 changes.

## MQ Architecture Note: Per-Peer Inbox Queues vs Per-Edge Queues

The current ConcreteMQ backend makes a deliberate tradeoff in how Abstract
channels are represented with Boost/POSIX message queues.

Abstract QUANTAS has channel-like links between peers. A literal IPC translation
would create one queue per directed topology edge:

```text
queue_0_to_1
queue_1_to_0
queue_0_to_2
...
```

The current MQ implementation instead creates one named inbox queue per peer:

```text
peer_0
peer_1
peer_2
...
```

Topology is enforced by neighbor assignments in `NetworkInterfaceConcreteMQ`.
A sender first checks whether the destination is in its neighbor set. If it is,
the packet is serialized and sent to the destination peer inbox. If it is not,
the send is ignored.

This keeps the number of OS message queues linear in the number of peers:

```text
per-peer inbox model: N queues
per-directed-edge model: N * (N - 1) queues for a complete topology
```

For dense experiments this matters because POSIX message queues have system
limits and setup/cleanup costs. A 100-peer complete topology would require 100
queues with the per-peer inbox model, but 9900 directed queues with a per-edge
model.

The cost of this tradeoff is that one inbox no longer directly represents one
Abstract `Channel`. All messages sent to a peer share the same OS inbox, so
per-channel behavior must be reproduced explicitly in the MQ transport layer.
For M2 distribution/channel parity, the MQ backend must still model Abstract
semantics such as:

- delay;
- drop probability;
- duplicate probability;
- reorder probability;
- per-round receive caps such as `maxMsgsRec`;
- queue-size behavior.

In short: MQ queues represent peer mailboxes, while C++ runtime logic represents
topology and channel semantics. This is simpler and more scalable than one queue
per topology edge, but it makes the M2 channel-semantics layer mandatory for
behavioral parity with Abstract QUANTAS.

## MQ Architecture Observation: Independent Process Rounds

Abstract QUANTAS is a lockstep round-based simulator. All peers live in one
process, and the simulator waits for the receive phase and compute phase to
complete across all peers before starting the next round.

The current ConcreteMQ backend intentionally moves execution toward independent
processes. The normal MQ run path launches one leader process and N peer
processes. After startup coordination, each peer process advances its own local
round loop and communicates through Boost/POSIX message queues.

This means there is currently:

- startup synchronization: peers create inboxes, send ready, receive assignments,
  and wait for the leader start signal;
- shutdown coordination: peers notify completion and wait for stop;
- no global per-round IPC barrier that forces every peer to finish round N before
  any peer starts round N+1.

This is an important semantic choice to observe and study. Independent peer
processes do not naturally wait for each other; they progress according to OS
scheduling, local computation speed, and message availability. That is closer to
a real process/network communication layer than the Abstract simulator's
centralized lockstep execution.

The tradeoff is:

```text
Strict Abstract parity:
  add a per-round IPC barrier
  preserve lockstep round semantics
  easier backend-to-backend comparison

Realistic process-based communication:
  keep independent peer progress
  model process scheduling and IPC timing more naturally
  accept semantic divergence from Abstract lockstep rounds
```

Future work should decide whether ConcreteMQ is intended to be:

1. a strict behavioral-parity backend for Abstract QUANTAS, in which case a
   per-round barrier is required; or
2. a more realistic process-based backend for studying algorithms over an IPC
   communication layer, in which case the absence of a global round barrier
   should be documented, measured, and treated as part of the model.

Until that decision is finalized, MQ results that depend on
`RoundManager::currentRound()` or lockstep delivery assumptions should be treated
carefully and compared against Abstract results with this semantic difference in
mind.

## MQ Architecture Observation: Packet Serialization Boundary

Abstract QUANTAS can pass `Packet` objects through in-memory channels because all
peers live inside one process and share the same address space.

ConcreteMQ peers run in separate processes. A process cannot read another
process's C++ objects directly, so packets must cross an explicit serialization
boundary before they enter Boost/POSIX message queues.

The current MQ backend satisfies this requirement for message passing:

- `Packet` defines Boost serialization support;
- the packet source, target, send timestamp, and JSON body are serialized;
- the JSON payload is converted to a string with `dump()` before serialization;
- the receiver reconstructs the JSON payload with `nlohmann::json::parse(...)`;
- `NetworkInterfaceConcreteMQ::unicastTo(...)` serializes a `Packet` into a byte
  string before `message_queue::timed_send(...)`;
- `NetworkInterfaceConcreteMQ::receive(...)` reads raw bytes from the inbox queue
  and deserializes them back into a `Packet`.

The same serialization pattern is also used for `MqAssignment`, allowing the
leader to send topology assignments to peer processes.

This satisfies the payload boundary needed for process-based packet delivery.
The important caveat is that this only serializes transport messages and
assignments. It does not serialize full `Peer` objects, algorithm-local runtime
state, network interface handles, or process-local resources. That distinction
matters for future metric aggregation, global hook behavior, and any design that
tries to reconstruct full peer state outside the owning process.
