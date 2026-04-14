# Gap Closing Plan

Resources and topics to study to build formal CS foundations alongside the internship.

---

## 1. C++ Patterns (from QUANTAS codebase)

These patterns appear repeatedly in the codebase. Study each one until you can explain it by name.

| Pattern | Where in QUANTAS | Resource |
|---|---|---|
| Smart pointers (`shared_ptr`, `unique_ptr`) | Channel creation | learncpp.com Chapter 22 |
| Polymorphism and `dynamic_cast` | NetworkInterface casting | learncpp.com Chapter 25 |
| Abstract classes and virtual functions | `Peer::performComputation() = 0` | learncpp.com Chapter 25 |
| Static initializers | PeerRegistry registration in each algo .cpp | learncpp.com Chapter 7.9 |
| Singleton pattern (Meyers Singleton) | LogWriter, RoundManager, PeerRegistry | learncpp.com Chapter 15 |
| Factory pattern | PeerRegistry::makePeer() | refactoring.guru/design-patterns/factory-method |
| Mutex and lock_guard | LogWriter thread safety | learncpp.com Chapter 24 |
| Templates | LogWriter::pushValue<T> / setValue<T> | learncpp.com Chapter 26 |

---

## 2. Distributed Systems Theory

Core concepts that directly apply to the internship project.

| Concept | Why it matters | Resource |
|---|---|---|
| Logical clocks / Lamport timestamps | Replacing RoundManager in Docker deployment | Search: "Lamport clock distributed systems" |
| Consensus algorithms | PBFT, Raft — algorithms already in QUANTAS | Search: "Raft consensus algorithm explained" (the Raft paper visualization at thesecretlivesofdata.com/raft) |
| Byzantine fault tolerance | ByzantinePeer / FaultManager in QUANTAS | Search: "Byzantine generals problem explained" |
| Synchronous vs asynchronous models | Key design decision for Docker backend | Search: "synchronous vs asynchronous distributed systems" |
| Message passing vs shared memory | Abstract (message passing) vs threads (shared memory) | Search: "message passing vs shared memory distributed systems" |

---

## 3. Networking Fundamentals

Concepts simulated by Channel and pushPacket().

| Concept | Where in QUANTAS | Resource |
|---|---|---|
| What is a packet | Packet.hpp | cloudflare.com/learning/network-layer/what-is-a-packet |
| Packet loss | dropProbability in Channel | cloudflare.com search "packet loss" |
| Network latency | minDelay/maxDelay in Channel | cloudflare.com search "network latency" |
| Packet duplication | duplicateProbability in Channel | Search: "packet duplication networking" |
| Unicast vs broadcast vs multicast | unicastTo, broadcast, multicast in Peer.hpp | Search: "unicast broadcast multicast explained" |

---

## 4. Simulation Theory

QUANTAS is a discrete event simulator. Understanding this makes the round-based model clear.

| Concept | Why it matters | Resource |
|---|---|---|
| Discrete event simulation | QUANTAS round model is a DES variant | CMU article: cs.cmu.edu/~music/cmp/archives/cmsip/readings/intro-discrete-event-sim.html |
| Stochastic systems | Probability-based channel behavior (drop, delay) | Start with Khan Academy "Basic Probability" |
| Probability distributions (uniform, Poisson) | DelayStyle in Channel: DS_UNIFORM, DS_POISSON | Khan Academy "Probability distributions" |

---

## 5. Docker and Deployment (for next phase)

Topics to study before starting the Docker backend implementation.

| Concept | Why it matters | Resource |
|---|---|---|
| Docker fundamentals | Each peer becomes a container | docs.docker.com/get-started |
| Docker networking | Containers need to communicate | docs.docker.com/network |
| Containernet | Mininet + Docker integration | Search: "Containernet github" |
| Mininet | Network topology emulation | mininet.org |
| gRPC or ZeroMQ | Potential inter-container communication library | grpc.io/docs or zeromq.org |

---

## 6. Formal CS Vocabulary

Concepts you understand but need the proper name for. Use these terms in meetings and reports.

| Your description | Formal name |
|---|---|
| "the global clock thing" | Logical clock / Lamport timestamp |
| "the factory thing" | Factory pattern |
| "the one shared object" | Singleton pattern |
| "checking what type it really is" | Dynamic dispatch / runtime polymorphism |
| "the base class that forces you to implement functions" | Abstract class / pure virtual functions |
| "the pipe between peers" | Unicast channel / directed link |
| "the string connecting two peers" | Directed edge (graph theory) |
| "steps of the simulation" | Discrete time steps / rounds |
| "rebuilding the system each test" | Independent trials (statistics) |
| "the inbox" | Input buffer / message queue |

---

## 7. Papers to Read

One paper per week. Even 40% understanding is valuable — mention findings in supervisor meetings.

| Week | Paper / Resource | Why |
|---|---|---|
| 1 | QUANTAS paper (already read) | Foundation of the project |
| 2 | "Time, Clocks, and the Ordering of Events" — Leslie Lamport (1978) | Foundational distributed systems paper, directly relevant to Docker deployment |
| 3 | Containernet paper — search "Containernet: Using Docker Containers as Mininet Hosts" | Directly relevant technology for the internship |
| 4 | Raft consensus paper — search "In Search of an Understandable Consensus Algorithm" | One of the algorithms implemented in QUANTAS |
| 5 | PBFT paper — search "Practical Byzantine Fault Tolerance" Castro & Liskov | Another algorithm in QUANTAS, relevant to fault injection |
| 6 | Docker networking internals — search "Docker networking design philosophy" | Understanding container communication for implementation |

---

## How to Use This Document

- Check off topics as you study them
- After studying a concept, add the formal name to your vocabulary (Section 6)
- Before each supervisor meeting, pick one thing from this list to mention
- Update this document as new gaps are discovered
