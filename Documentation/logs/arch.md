# Questions Before Implementing ConcreteSimulationMQ.cpp

[GitLab - ConcreteMQ](https://gitlab.lip6.fr/godawatta/Quantas/-/tree/e44deec99d80ced05ce7d5981371904fc5123712/quantas/Common/ConcreteMQ)

## What I am trying to do

The next file I need to write is `ConcreteSimulationMQ.cpp` — the `main()` that starts a peer process, connects it to the coordinator, and runs the algorithm round by round. Before I write it, I want to lock down the design so I do not have to rewrite later when we move to 0MQ or Docker/Mininet

## The core design constraint

Whatever the current abstract QUANTAS does — the metrics it collects, the way rounds work in lockstep, the single combined output file it produces, every topology it supports — the MQ version must also produce. And later, the 0MQ + Docker + Mininet version must produce the same thing too.

The application layer (algorithm files like `BitcoinPeer.cpp`, `ExamplePeer.cpp`) cannot be changed. The framework can. So the framework has to do whatever it takes to keep the behaviour identical across all three versions.

This constraint resolves most of the design questions on its own. Below I have separated what I am already going to do (for your awareness) from the questions that need your input.

---

## Decisions I Am Making...

These are decided, either by the design constraint above or by reasoning about what the next phase (0MQ) will need. I am listing them so you can flag any you disagree with.

| # | Decision | Reasoning |
|---|---|---|
| 1 | **Two separate binaries**: `quantas_logger` and `quantas_peer`. No `--leader` flag. | The roles are different at compile time, and this maps cleanly to two Docker images later. |
| 2 | **Coordinator assigns peer IDs dynamically.** Each peer registers via a temp reply queue and gets back an ID `0..N-1`. | Peer processes are interchangeable, which is what Docker containers need. |
| 3 | **Add `setNetworkInterface()` to `Peer.hpp`.** After `PeerRegistry::makePeer()`, swap the abstract interface for `NetworkInterfaceConcreteMQ`. | One change to the framework, zero changes to algorithms. Same pattern works for 0MQ later. |
| 4 | **Logger aggregates metrics into one combined output file.** | Constraint: current QUANTAS produces one file, MQ must too. |
| 5 | **Fixed round count from JSON config** for the stop mechanism. | Constraint: matches abstract simulation. |
| 6 | **Support every topology the current QUANTAS supports** (complete, ring, random, kademlia, etc.). | Constraint: behavioural parity. |
| 7 | **Separate the start/stop control queue from the data queue per peer.** | 0MQ idioms favour one socket per concern, so doing it now means no rewrite later. |
| 8 | **`receive()` drains all messages each round.** | Matches the "all messages arrived this round" semantics of the abstract simulation. |
| 9 | **Raise `mq_msgsize` via `sysctl`** for the 1024-byte limit. No fragmentation logic. | 0MQ has no such limit, so any fragmentation code would be thrown away. |
| 10 | **Latency timestamps go through `OutputWriter`**, not stdout. | Matches how the abstract simulation handles metrics. |
| 11 | **Launch via a `Makefile` target** for now. | Reproducible, and maps to `docker compose up` later. |
| 12 | **No `CoordinatorAddress` abstraction yet.** Phase 1 uses a fixed queue name; defer until 0MQ forces it. | Premature abstraction. |

---

## Open Questions — your input needed

There are three for the moment. The first one is the real architectural problem; the other two are quick confirmations.

---

### 1. The hardest problem: how do `endOfRound`, `initParameters`, and `endOfExperiment` keep working in MQ mode?

In the current QUANTAS, these functions are called with the full list of all peers, and algorithms iterate over the list:

```cpp
void endOfRound(vector<Peer*>& peers) override {
    int total = 0;
    for (auto* p : peers) total += p->throughput;
    OutputWriter::pushValue("throughput", total);
}
```

In MQ mode, each process only has its own peer. If the framework calls `endOfRound([this_peer])`, the loop only sees one peer, and the metric is silently wrong. Since we cannot change algorithm code, the framework has to give these functions something that *behaves* like the full peer list — which means peer state needs to cross process boundaries somehow.

The two approaches I can think of:

- **(a)** At the end of each round, every peer process serializes its peer state and sends it to the logger. The logger reconstructs a full peer list and calls `endOfRound([all peers])` itself. Requires `Peer` to be serializable end-to-end.
- **(b)** Each peer pushes its own metrics independently and the logger aggregates them outside the `endOfRound` call. But this would require changing how the algorithm collects metrics, which is an application-layer change — not allowed.

**(a)** seems like the only option that respects the constraint, but it is a lot of work and I want to know if you have a better approach in mind before I commit. This same problem applies to `initParameters` and `endOfExperiment`.

---

### 2. How does each peer learn its neighbour set? Quick confirmation.

Two reasonable options:

- **(i)** The coordinator computes the topology and tells each peer its neighbour list when it assigns the ID.
- **(ii)** Each peer reads the JSON config itself and computes its neighbours.

I am leaning toward **(i)** because the coordinator already owns the system view, but **(ii)** is cleaner for the Docker phase where each container already has the config file. Which fits your plan?

---

### 3. Round semantics in async mode — confirming option (ii).

The design constraint already implies the answer (synchronized rounds, since algorithms assume lockstep), but I want to confirm before I commit because synchronization adds a coordination message every round.

- **(i)** Each peer loops freely, no shared round counter. Fast but breaks any algorithm that compares rounds across peers.
- **(ii)** Peers wait for the coordinator's "go to round N+1" signal each round. Slower but preserves abstract semantics.
- **(iii)** Wall-clock interval rounds. Decouples rounds from messages.

Confirming **(ii)** is what you want?
