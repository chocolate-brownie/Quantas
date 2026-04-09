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
- There are 3-4 C++ patterns that keep appearing in this codebase that I have to take a separate day and study further it will make reading code a bit more easier for me
    1. `shared_ptr` and ownership — I hit this today with channels
    2. Polymorphism and `dynamic_cast` — I'll hit this more as I go deeper into the interface layer
    3. Abstract classes and virtual functions — Peer has virtual void `performComputation() = 0`, this pattern is everywhere (I have used this in previous projects)
    4. Static initializers — I've gotta study a bit further about this as well

A good free resource is learncpp.com — it's structured, example-driven, and covers exactly these topics at the right level. Specifically:

    - Chapter 22 — smart pointers (shared_ptr, unique_ptr)
    - Chapter 25 — virtual functions and abstract classes
    - Chapter 23 — dynamic_cast

### 09/04/2026

- Finished studying `createInitialChannels()` in full:
    1. A `shared_ptr<Channel>` is created for each directed peer→neighbor link (one object, shared ownership)
    2. `dynamic_cast<NetworkInterfaceAbstract*>` is used to convert the base `NetworkInterface*` pointer to the concrete type so we can call `addInboundChannel()` / `addOutboundChannel()`
    3. The same channel pointer is registered as **inbound** on the receiver's interface and **outbound** on the sender's interface — this is how the "pipe" gets attached to both ends
    4. For a complete 3-peer network: 6 directed channels total (each peer × 2 neighbors)
- Studied `initParameters()` — reads algorithm-specific config from JSON and sets it on all peers before rounds start. For AltBit: `timeOutRate = 2` (wait 2 rounds before resending an unacknowledged message)
- Understood the round loop structure — 4 parts per round:
    1. `RoundManager::incrementRound()` — tick the global clock by 1
    2. `receive()` — all peers check inbound channels for arrived packets (parallel)
    3. `tryPerformComputation()` — all peers run their algorithm logic (parallel)
    4. `endOfRound()` — collect metrics
- `RoundManager` is a global counter visible to every part of the simulation — packets use it to check if their delay has elapsed, peers use it to check timeouts
- Distinction: **tests** (outer loop) = how many times the distributed system is rebuilt; **rounds** (inner loop) = how many times the algorithm runs on one system
- Supervisor asked to study `LogWriter` next
- Tomorrow: study `LogWriter` — how metrics are collected and written to output files
