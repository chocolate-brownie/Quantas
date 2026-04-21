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
| System clock synchronization | Replacing RoundManager in real deployment — system clock chosen over Lamport because Lamport cannot compute latency (no real time) | Search: "NTP clock synchronization distributed systems" |
| Consensus algorithms | PBFT, Raft — algorithms already in QUANTAS | Search: "Raft consensus algorithm explained" (the Raft paper visualization at thesecretlivesofdata.com/raft) |
| Byzantine fault tolerance | ByzantinePeer / FaultManager in QUANTAS | Search: "Byzantine generals problem explained" |
| Synchronous vs asynchronous models | Key design decision for Docker backend | Search: "synchronous vs asynchronous distributed systems" |
| Message passing vs shared memory | Abstract (message passing) vs threads (shared memory) | Search: "message passing vs shared memory distributed systems" |

---

## 3. Networking Fundamentals

Concepts simulated by Channel and pushPacket().

| Concept | Where in QUANTAS | Resource |
|---|---|---|
| ~~What is a packet~~ | Packet.hpp | cloudflare.com/learning/network-layer/what-is-a-packet |
| ~~Packet loss~~ | dropProbability in Channel | cloudflare.com search "packet loss" |
| ~~Network latency~~ | minDelay/maxDelay in Channel~~ | cloudflare.com search "network latency" |
| ~~Packet duplication~~ | duplicateProbability in Channel~~ | Search: "packet duplication networking" |
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

## 5. Phase 1 — Multi-Process on One Machine (current task, due 22/04/2026)

Before Docker/Mininet: move from 1 process to N+1 processes on the same machine. Each peer becomes its own OS process. Processes communicate via Boost message queues.

| Concept | Why it matters | Resource |
|---|---|---|
| **Boost message queues (PRIORITY)** | Replaces in-memory Channel deque — sends messages between separate processes on the same machine | Book: "The Boost C++ Libraries" by Boris Schaling — interprocess communication chapters (assigned by supervisor) |
| Interprocess communication (IPC) | The general concept — how separate processes exchange data without shared memory | Search: "interprocess communication explained" |
| How to design the new concrete communication class | `unicastTo()` sends via Boost message queue; `receive()` reads from it into `_inStream` | Think through this yourself as part of the task |
| **Distributed Algorithms for Message-Passing Systems** — Michel Raynal (PRIORITY) | Foundational textbook assigned by supervisor — use as reference, not cover-to-cover | Book assigned 15/04/2026 |

---

## 6. Phase 2 — Docker and Mininet (after Phase 1)

Topics to study before the Docker backend implementation.

| Concept | Why it matters | Resource |
|---|---|---|
| Docker fundamentals | Each peer becomes a container | docs.docker.com/get-started |
| Docker networking | Containers need to communicate | docs.docker.com/network |
| Containernet | Mininet + Docker integration | Search: "Containernet github" |
| Mininet | Network topology emulation — creates virtual network (routers, switches, links) in software | mininet.org |
| Boost.Asio | Network socket communication (UDP) — for inter-container messaging in Phase 2 | "The Boost C++ Libraries" by Boris Schaling — networking chapters |
| UDP protocol | Chosen for inter-container messaging — no delivery guarantee, matches QUANTAS channel behavior (drop/delay/reorder) | Search: "TCP vs UDP explained", Cloudflare "what is UDP" |
| TCP vs UDP | Understanding why UDP was chosen — TCP hides packet loss, UDP exposes it | Search: "TCP vs UDP difference" |

---

## 7. Formal CS Vocabulary

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

## 8. Papers to Read

One paper per week. Even 40% understanding is valuable — mention findings in supervisor meetings.

| Week | Paper / Resource | Why |
|---|---|---|
| 1 | QUANTAS paper (already read) | Foundation of the project |
| 2 | *Distributed Algorithms for Message-Passing Systems* — Michel Raynal (assigned 15/04/2026) | Core textbook for Phase 1 — message passing between processes |
| 3 | "Time, Clocks, and the Ordering of Events" — Leslie Lamport (1978) | Foundational distributed systems paper, directly relevant to deployment |
| 4 | Containernet paper — search "Containernet: Using Docker Containers as Mininet Hosts" | Directly relevant technology for Phase 2 |
| 5 | Raft consensus paper — search "In Search of an Understandable Consensus Algorithm" | One of the algorithms implemented in QUANTAS |
| 6 | PBFT paper — search "Practical Byzantine Fault Tolerance" Castro & Liskov | Another algorithm in QUANTAS, relevant to fault injection |

---

## 9. Daily 1-Hour Compounding Plan

Four skills to build alongside the internship. 1 hour per day, every day.

---

### The Four Skills

| Skill | Why it matters |
|---|---|
| Abstraction and formal reasoning | Think in systems, not just code |
| Handling unfamiliar complex systems | Build mental models faster |
| Architecture, design tradeoffs, long-term planning | Make better decisions earlier |
| Communicating with engineers, researchers, leadership | Land ideas clearly in meetings and reports |

---

### Weekly Schedule

| Day | Activity | Time |
|---|---|---|
| Monday | SICP or Kleinberg — 1 chapter | 1 hour |
| Tuesday | Designing Data-Intensive Applications — 1 section | 1 hour |
| Wednesday | SICP or Kleinberg — 1 chapter | 1 hour |
| Thursday | Designing Data-Intensive Applications — 1 section | 1 hour |
| Friday | Revisit anything from the week that didn't click | 1 hour |
| Saturday | One paper from Section 8 — even 40% understanding is enough | 1 hour |
| Sunday | Rest | — |

---

### Books

| Skill | Book | Why |
|---|---|---|
| Abstraction and formal reasoning | *Structure and Interpretation of Computer Programs* (SICP) — free online | Teaches thinking in abstractions, not just writing code |
| Abstraction and formal reasoning (alternative if SICP is too dense) | *How to Think About Algorithms* — Jacob Kleinberg | More accessible entry point |
| Architecture and design tradeoffs | *Designing Data-Intensive Applications* — Martin Kleppmann | Best book on system design tradeoffs — directly relevant to distributed systems work at LIP6 |

---

### Handling Unfamiliar Complex Systems — No Extra Time Needed

Before reading any new code, write down what you *expect* to find.
After reading, write what was *different*.

5 minutes before + 5 minutes after each code study session. This trains the mental model building that comes naturally from structured academic problem sets.

---

### Communication — No Extra Time Needed

After every supervisor meeting, write 3 bullet points:
- What I said that landed well
- What I said that was unclear
- What I should have said instead

Researchers communicate in a consistent pattern: **problem → constraint → proposed solution → tradeoff**. Practicing this after each meeting builds that pattern naturally.

---

### One Rule

Don't add more books. Finish what you start. Depth over breadth.

---

## How to Use This Document

- Check off topics as you study them
- After studying a concept, add the formal name to your vocabulary (Section 7)
- Before each supervisor meeting, pick one thing from this list to mention
- Update this document as new gaps are discovered
