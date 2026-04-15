# Phase 1 Implementation Plan
**Meeting date**: 15/04/2026  
**Next review**: Wednesday 22/04/2026

---

## Current State

- 1 machine
- 1 process
- Abstract simulation — peers are objects in shared memory communicating through in-memory Channel deques

---

## Phase 1 Target

- 1 machine
- N+1 processes:
  - 1 process per peer (each executes the code of one node)
  - 1 additional process for the logger
- Communication between processes via **Boost message queues** (IPC)

> This is no longer a simulation — each process IS the node.

---

## Main Constraint

**Application code must remain the same.**

- API is preserved
- JSON config file is preserved
- From the user's point of view there is no difference between abstract QUANTAS and concrete QUANTAS

---

## What Needs to Change

A new concrete communication class that replaces `NetworkInterfaceAbstract`:

| Component | Abstract | Phase 1 (Concrete) |
|---|---|---|
| Peer | Object in shared memory | Separate OS process |
| Communication | In-memory Channel deque | Boost message queue (IPC) |
| Logger | Singleton in shared memory | Separate process |
| NetworkInterface | `NetworkInterfaceAbstract` | New concrete class (TBD) |
| Algorithm code | Unchanged | Unchanged |
| JSON config | Same | Same |

---

## Clock Decision

**System clock** (real wall clock) will be used — not Lamport clocks.

Reason: Lamport clocks have no real time — they only give ordering of events. You cannot compute latency (how long a message took to travel) from a Lamport timestamp. Since QUANTAS measures performance (latency, throughput), real timestamps are required.

Consequence: processes need to have synchronized clocks. This is a known distributed systems problem solved by NTP (Network Time Protocol) in practice.

---

## Tasks Until Wednesday 22/04/2026

1. **Study Boost message queues** — from "The Boost C++ Libraries" by Boris Schaling, interprocess communication chapters
2. **Think about how to plug it into a new concrete communication class in QUANTAS** — how would `unicastTo()` and `receive()` work using Boost message queues instead of Channel deques?
3. **Read**: *Distributed Algorithms for Message-Passing Systems* — Michel Raynal

---

## Key Question to Answer by Wednesday

How does `unicastTo()` send a message to another process using a Boost message queue?  
How does `receive()` read from a Boost message queue into `_inStream`?

---

## Why This Step Before Docker/Mininet

Moving from 1 process to N processes on 1 machine is the smallest possible step toward real deployment. It:
- Breaks shared memory dependency
- Introduces real IPC without network complexity
- Validates that the API/constraint holds before adding Docker and Mininet on top
