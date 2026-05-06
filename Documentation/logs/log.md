In this file I document what I do everyday during my internship.

### 01/04/2026

- I studied a little bit about distributed systems and algorithms and how they works. How an algorithm of these systems always has to be data consistent, fault tolerance, concurrency controlled and resource managed.
- Setup GITLAB and Developer environment
- Reading the paper about Quantas

### 02/04/2026

- Continuing to read the Quantas paper. Following the sim.run() function at Simulation.hpp file, learning about different components in the JSON files and how they connects with the testing of algorithm.
- How performance can be enhanced using threads for each peer to run performComputation()
- The JSON config doesn't just connect to "testing" — it defines the entire experiment: topology, network conditions, algorithm parameters, and how many repetitions to run. Testing is one use, but so is data collection for research

### 03/04/2026

- Continuing to read the Quantas paper, following the code
- Today I understood most of the stuff in the simulation componenet, until we hit the for loop that runs number of tests times mentioned in the JSON file.. the `Simulation.run()` kinda preps the by extracting data from the JSON, setting up threads, managing the output logfile. Now I am reading the `Network.hpp` and `Network.cpp` which comes in to play inside the for loop. It does three things in order
    1. Create peers (lines 29-32) — uses PeerRegistry::makePeer() to
       instantiate the right peer type from the JSON string
    2. Set up neighbors (lines 42-64) — picks the topology function
       (fullyConnect, chain, ring, etc.) and calls addNeighbor() on each peer
    3. Create channels (line 66) — createInitialChannels() wires up actual Channel objects between every pair of neighbors

- I have to go in depth of what these three things do

### 07/04/2026

- Continuing to read the Quantas paper and following the code accordingly
- Today studied `void Network::initNetwork(json topology)` — the function that builds the distributed system for each test run. Understood three things it does in order:
    1. Creates peer objects using `PeerRegistry::makePeer()` and stores them in `_peers`
    2. Wires neighbor relationships according to the topology type (e.g. `"complete"` means every peer knows every other peer's ID). At this point peers only know their neighbors' IDs — no communication channels yet.
    3. Calls `createInitialChannels()` to create the actual Channel objects between neighbors (not yet studied)
- Also learned that before `main()` runs, each algorithm's `.cpp` file has a `static bool` variable at file scope that C++ initializes automatically at program startup. This calls `PeerRegistry::registerPeerType("AlgoName", factory)`, storing a factory function in a map keyed by the algorithm name. When `makePeer("AlgoName", id)` is called later, it looks up that map and calls the stored factory to create the correct peer type.
- Tomorrow: study `createInitialChannels()` — how actual Channel objects get wired between neighbors after topology is set up.

### 08/04/2026

- Today I have been studying the `createInitialChannels()` function. It walks through every peer in the network, for each peer it gets its neighbor list. Then for each neighbor list it will create one channel. The channel creation is a bit confusing to me. I understood the thing about where it creates a bidirectional connection for the communication between a peer's known neighbors back and forth, but the code itself is a bit confusing to understand.

### 09/04/2026

- Finished studying `createInitialChannels()` in full:
    1. A `shared_ptr<Channel>` is created for each directed peer→neighbor link (one object, shared ownership)
    2. `dynamic_cast<NetworkInterfaceAbstract*>` is used to convert the base `NetworkInterface*` pointer to the concrete type so we can call `addInboundChannel()` / `addOutboundChannel()`
    3. The same channel pointer is registered as **inbound** on the receiver's interface and **outbound** on the sender's interface — this is how the "pipe" gets attached to both ends
    4. For a complete 3-peer network: 6 directed channels total (each peer × 2 neighbors)
- Studied `initParameters()` — reads algorithm-specific config from JSON and sets it on all peers before rounds start. For AltBit: `timeOutRate = 2` (wait 2 rounds before resending an unacknowledged message)
- Working on the round loop structure.

### 09/04/2026

- Finished studying `incrementRound()` function, how ROUNDS works. Basically 1 round is considered as when a peer is completed receive and computation. Round get incremented each time for the peers to know about the current round that they are in. This concept of rounds has something to do with something called _discrete even simulation_
- I have started studying the LogWriter to understand what kind values are pushed by each algorithm to the logger
- Things to study

### 13/04/2026

- Studied how `pushPacket()` function works. It simluates the imperfect network conditions such as when sending the messages packets gettings lost, arriving late, arriving twice. This is to make sure that during a distributed algorithm testing the algorithm should perform under all kinda of network conditions.

### 14/04/2026

- Continued studying `pushPacket()` in depth — drop probability using `trueWithProbability()`, how `uniformReal(0.0, 1.0) < p` simulates randomness, and how drop/delay/duplicate are three independent network problems
- Studied `LogWriter` fully — Meyers Singleton pattern, mutex thread safety, `pushValue()` vs `setValue()`, how peer variables (messagesSent, requestsSatisfied) flow into the output JSON file
- Studied `NetworkInterfaceAbstract` — the two key functions `unicastTo()` (send side) and `receive()` (receive side), and how this is the only file that needs to be replaced for Docker deployment
- Understood the full real deployment stack: Docker (containers) + Mininet (virtual network) + Boost IPC (inter-process communication) + UDP (protocol)
- Supervisor meeting tomorrow at 14:00 — prepared talking points and questions
- Professor assigned: study Boost C++ Libraries by Boris Schaling — specifically interprocess communication chapters

### 15/04/2026

- Supervisor meeting at 14:00 — attended and presented understanding of the abstract simulation
- Learned the Phase 1 deployment target: 1 machine, N+1 processes (1 per peer + 1 for the logger), communicating via Boost message queues. This is the step before Docker/Mininet.
- Main constraint confirmed by supervisor: application code must remain unchanged — API and JSON config are preserved. From the user's point of view there is no difference between abstract QUANTAS and concrete QUANTAS.
- Learned that system clock (real wall time) will be used instead of Lamport clocks — Lamport clocks cannot compute latency because they have no real time, only event ordering.
- Understood the full 3-phase deployment roadmap:
    1. Abstract simulation (current) — 1 process, in-memory Channel deques
    2. Phase 1 (next) — 1 machine, N+1 processes, Boost message queues replace Channel deques
    3. Phase 2 (later) — Docker containers + Mininet network + UDP sockets
- Professor assigned: study Boost message queues from "The Boost C++ Libraries" by Boris Schaling and think about how to design the new concrete communication class
- Professor assigned: read *Distributed Algorithms for Message-Passing Systems* by Michel Raynal as a reference textbook
- Tasks before Wednesday 22/04/2026: understand Boost message queue API, design how `unicastTo()` and `receive()` would work in the new concrete class

### 16/04/2026

- Spent the morning deeply understanding the architecture behind the Phase 1 task assigned by the professor
- Understood why Phase 1 exists: before adding Docker and Mininet, we first need to prove that inter-process communication is possible on the same machine without shared memory, while keeping the API constraint (algorithm code unchanged)
- Understood what "1 machine, 1 process" means: currently all peers, channels, LogWriter, and RoundManager are C++ objects living in the same memory space — any object can reach any other object directly
- Understood the full replacement table for real deployment:
    - Channel deque (shared memory) → Boost message queues (Phase 1) → UDP over Mininet (Phase 2)
    - RoundManager (shared counter) → NTP synchronized system clock (each peer reads locally)
    - LogWriter (shared singleton) → separate logger process (peers send metrics via messages)
- Understood the 3-phase roadmap clearly:
    1. Simulation: 1 machine, 1 process, shared memory
    2. Phase 1: 1 machine, N+1 processes, Boost message queues (proves API constraint without network complexity)
    3. Phase 2: N machines (Docker containers), UDP over Mininet (full real deployment)
- Key insight: the shift from simulation to real deployment is fundamentally a shift from shared memory to message passing — peers can no longer access each other's memory directly, everything must be communicated through messages
- What needs to be designed by Wednesday: a new `NetworkInterfaceConcrete` class where `unicastTo()` puts messages into a Boost message queue and `receive()` reads from one — algorithm code calls the same functions and sees no difference

### 17/04/2026

- Continued reading the Boost message_queue chapter — locked in the 4-call API: `create_only`, `open_only`, `send`, `try_receive`/`receive`, `remove`
- Hands-on exercises in `Study/Boost/msgq/`: wrote `p1.cpp` (sender) and `p2.cpp` (receiver), got two processes to exchange 10 integers via a named queue
- Debugged two failures along the way:
    1. `library_error` thrown when creating queue with capacity 100 — root cause: POSIX system limit `/proc/sys/fs/mqueue/msg_max = 10`. Fixed by lowering capacity to 10.
    2. `library_error` (code 18, size_error) on receive — root cause: receive buffer size must be `>= max_msg_size` of the queue. Fixed by matching buffer to `sizeof(int)`.
- Studied existing `quantas/Common/Concrete/NetworkInterfaceConcrete.hpp` to understand the TCP-based variant: `unicastTo()` calls `send_json()` over sockets, `start_listener()` runs a background thread doing `accept()` + `read()` and pushes into `_inStream`
- Architecture decision: create a new parallel folder `quantas/Common/ConcreteMQ/` instead of modifying the existing `Concrete/` (TCP version stays as reference, supervisor sees a clean diff, three coexisting backends — Abstract, Concrete (TCP), ConcreteMQ — become an asset)
- Decided to use `NetworkInterfaceAbstract.hpp` as the skeleton (not the TCP `Concrete`) because Boost MQ supports `try_receive()` (non-blocking polling), which matches Abstract's round-synchronous `receive()` model perfectly — no listener thread needed

### 20/04/2026

- `Study/Boost/msgq/send.cpp`, `Study/Boost/msgq/recv.cpp` Wrote a program that can turn a json object into rawdata and parse it to message queue and then able to receive from and parse it back to a JSON obj.
- I am trying to come up with a design that can replace the channel queue with boost message queue with the function unicast and receive

### 21/04/2026
- Today I spend time trying to come up with a solution on how I should implement the boost mq to `unicastTo()` and `receive()`. Then after reading the new updated TCP solution files I understood some possible solutions
- The main challenge with the setup is to solve the *barrier synchronization* problem in the tcp solution. A solution has been already implemented called the
*rendezvous protocol* I can implement the same thing but compatible for the boost mq.

### 22/04/2026

- Supervisor meeting with Sebastian — presented understanding of Boost MQ API and the Phase 1 design
- New requirements confirmed: logger process will act as the leader/coordinator in the rendezvous protocol, N message queues from peers to the logger (one per peer), and real wall-clock timestamps must be recorded at `send()` and `receive()` to measure actual message latency
- Continued implementing `NetworkInterfaceConcreteMQ` — header and `configure()` function completed

### 24/04/2026

- Split inbox creation between the two classes: `ProcessCoordinatorMQ::createInbox()` creates `peer_<id>` early with `create_only`, `NetworkInterfaceConcreteMQ::configure()` now just opens it with `open_only` — fixes the timing problem where the leader would send "start" before the queue existed.
- Implemented `createBarrier()`, `createInbox()`, `sendReady()`, and started `waitForAllReady()` in `ProcessCoordinatorMQ.cpp`. Adopted the colleague's error convention: `throw std::runtime_error("context: " + ex.what())` instead of local `exit(1)`, with `_myId` included so we know which peer failed.
- Key insight: Boost `message_queue` is just a handle — the OS queue persists after the C++ object dies, so any process can `open_only` a queue created elsewhere.
- Set up a minimal `.clang-format` that only locks in universally-consistent options (IndentWidth 4, BlockIndent, NamespaceIndentation None, ColumnLimit 100) so existing inconsistent files aren't churned.
- Next: finish `waitForAllReady()` loop over `_totalPeers`, then `broadcastStart()` / `waitForStart()` / `cleanUp()` — goal is a working rendezvous end-to-end by 29/04/2026.

### 27/04/2026

- Finished the rendezvous protocol end-to-end: implemented `waitForAllReady()`, `broadcastStart()`, `waitForStart()`, and `cleanUp()` in `ProcessCoordinatorMQ.cpp`. Split `cleanUp()` so the leader removes `mq_barrier` + all `peer_<id>` queues while a follower removes only its own — avoids race conditions where one process deletes another's still-in-use queue.
- Added `_myBarrier` as a private member alongside `_myInbox` so `createBarrier()` and `waitForAllReady()` share the same handle instead of re-opening with `open_only`. Removed the destructor's `cleanUp()` call after debugging revealed it was wiping queues that other processes still needed.
- Wrote a two-process smoke test in `Study/ConcreteMQ/leader.cpp` and `follower.cpp` to validate the protocol manually.
- Resolved the `MAX_MSG_SIZE` design question: the barrier carries only `unsigned int` triggers so it stays at `sizeof(unsigned int)`, but the inbox needs to handle JSON traffic later so it's sized at 1024 from the start (defined as `MAX_MSG_SIZE` in the header). Trade-off acknowledged: small memory waste during the rendezvous phase, but avoids the complexity of recreating queues after handshake.
- Started designing `unicastTo()` and `receive()` in `NetworkInterfaceConcreteMQ`. Decision: serialize/deserialize `Packet` via `to_json` / `from_json` free functions in the `quantas` namespace (not member functions) so nlohmann's argument-dependent lookup works. Phase 1 simplification: `_delay` and `_round` fields are not serialized since they're Abstract simulation concepts.
- Open question for the supervisor: what is the maximum expected JSON message size for Phase 1? — `MAX_MSG_SIZE = 1024` is a guess and PBFT messages may exceed it.

### 28/04/2026

- Switched `Packet` serialization to Boost (per professor's preference) — intrusive split `save()`/`load()` with `BOOST_SERIALIZATION_SPLIT_MEMBER()`. `_body` is dumped/parsed as a string since `nlohmann::json` is not natively Boost-serializable. `_delay`/`_round` not serialized.
- Implemented `unicastTo()`: build Packet → `binary_oarchive` into `stringstream` → open `peer_<dest>` with `open_only` → guard size with `mq.get_max_msg_size()` → `send()`.
- Implemented `receive()`: `try_receive()` loop on `_myInbox` → wrap `recvd_size` bytes in `stringstream` → `binary_iarchive` → push to `_inStream` under mutex. Loop exits when queue empty.
- Key intuition: `_myInbox` is a persistent handle to my own queue; the `mq` in `unicastTo()` is a temporary handle to the destination's queue. Same idea as file descriptors.

### 29/04/2026 - 30/04/2026

- Started designing `ConcreteSimulationMQ.cpp` — the per-process `main()` that ties together `NetworkInterfaceConcreteMQ` and `ProcessCoordinatorMQ` to run a real algorithm over MQ
- Locked in architectural decisions under the constraint that behaviour must match abstract QUANTAS and the application layer cannot be changed:
    - Two separate binaries (`quantas_logger`, `quantas_peer`)
    - Coordinator dynamically assigns peer IDs (peers register via a temp reply queue and get back an ID `0..N-1`) — interchangeable processes for the eventual Docker phase
    - Add `setNetworkInterface()` to `Peer.hpp` so any algorithm swaps from Abstract → MQ → 0MQ without per-algorithm subclasses
    - Logger aggregates metrics into a single combined output file; fixed round count from JSON; separate control vs data queues per peer; `receive()` drains all per round; raise `mq_msgsize` via sysctl rather than write fragmentation logic
- Professor confirmed the next phase will replace Boost MQ with ZeroMQ — design philosophy locked in: *throwaway code, non-throwaway protocol* (Boost MQ and 0MQ are the same async-messaging paradigm, so the protocol shape translates directly)

### 04/05/2026

- Added `setNetworkInterface()` to `Peer.hpp` so an existing peer can replace its original `NetworkInterfaceAbstract` with `NetworkInterfaceConcreteMQ` after construction.
- Created an AltBit real-peer MQ smoke test with `altbit_peer0.cpp` and `altbit_peer1.cpp` under `quantas/Common/ConcreteMQ/Tests`.
- Ran `make -C quantas/Common/ConcreteMQ/Tests run_altbit` successfully. This proves `AltBitPeer::performComputation()` can generate messages that travel through `NetworkInterfaceConcreteMQ` and Boost message queues between two peer processes.
- Key result: we moved from testing MQ directly with `iface.unicastTo()` to testing MQ through real QUANTAS algorithm code.

### 05/05/2026

- Consolidated `ConcreteMQ/Tests` around a single reusable `mq_peer_runner.cpp` that reads `experiments[0]`, `initialPeers`, `initialPeerType`, and `rounds` from JSON, with optional CLI round override for smoke runs.
- Updated `ConcreteMQ/Tests/makefile` to remove stale deleted targets and keep runnable paths (`run`, `run_mq_peer_runner`) aligned with files that currently exist in the folder.
- Produced a LIP6-style implementation status + roadmap document (`Documentation/LIP6_ConcreteMQ_Status_and_Roadmap.md`) with phased TODOs and acceptance tests from bring-up to abstract-vs-MQ benchmark comparison.

### 06/05/2026

- Added root makefile MQ targets to separate worker runtime from abstract runtime: `mq_release`, `mq_debug`, and `mq_run` now build/run `quantas_mq_peer.exe` independently of `quantas.exe`.
- Refactored `quantas/Common/ConcreteMQ/concreteSimulationMQ.cpp` by splitting data roles into `CliArgs` and `ExperimentConfig`, and separating config loading (`loadConfig`) from per-experiment parsing (`parseExperiment`).
- Updated MQ worker flow to iterate over `config["experiments"]` and perform per-experiment extraction + rendezvous + peer execution, while keeping current follower-worker semantics.
- Clarified the architecture gap vs TCP concrete: MQ currently has peer-worker + start barrier only; still missing full leader/logger orchestration, stop protocol, and metrics aggregation parity.
