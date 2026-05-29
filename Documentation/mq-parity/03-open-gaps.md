# Open Gaps (Parity-Critical)

These gaps explain why MQ can be operationally complete (J1-J14 baseline) but still not behaviorally identical to Abstract QUANTAS.

## Operational vs Behavioral Parity

- **Operational parity** means the runtime lifecycle executes end-to-end:
configure -> assign -> build -> init -> start -> rounds -> stop -> output -> cleanup.
- **Behavioral parity** means the same input contract produces equivalent semantics and interpretable outcomes as Abstract QUANTAS.

J1-J14 establishes operational baseline.  
The gaps below are required to close behavioral parity.

---

## G1: Topology mapping fidelity

### What it is
Topology is the communication graph: who can directly send to whom.

### First-principles reason
In distributed systems, the graph is part of the algorithm’s environment.  
If the graph changes, the algorithm behavior changes.

### Why current baseline is insufficient
A run can succeed operationally while still using simplified neighbor assignment that does not match requested topology (`ring`, `grid`, `userList`, etc.).

### Behavioral parity requirement
For the same `topology` JSON, MQ neighbor relationships must be semantically equivalent to Abstract mode graph construction.

---

## G2: Channel/distribution behavior fidelity

### What it is
Message transport semantics: delay, drop, duplicate, reorder, queue-size behavior.

### First-principles reason
Algorithm outcomes depend on transport perturbations, not only connectivity.

### Why current baseline is insufficient
MQ can deliver messages and complete lifecycle, but if channel behavior does not reflect configured distribution semantics, experiment meaning diverges from Abstract mode.

### Behavioral parity requirement
For the same distribution config, MQ must provide equivalent model behavior (statistically/semantically), including boundary conditions.

---

## G3: `tests > 1` semantics

### What it is
Repeated trial execution under one experiment definition.

### First-principles reason
Multiple tests are part of experiment semantics and necessary for robust interpretation under stochastic behavior.

### Why current baseline is insufficient
Running effectively one trial while warning on `tests > 1` changes the meaning of the experiment and its outputs.

### Behavioral parity requirement
MQ must execute repeated tests with proper re-initialization and per-test result accounting consistent with Abstract semantics.

---

## G4: Final `DoneSignals` policy

### What it is
Protocol-level rule for when an experiment is considered complete.

### First-principles reason
Termination is a correctness property. Stop condition must be explicit, deterministic, and protocol-authoritative.

### Why current baseline is insufficient
Baseline handshake wiring exists, but fallback/partial policy behavior can create ambiguity in stop semantics.

### Behavioral parity requirement
`DoneSignals` behavior must be finalized so shutdown is driven by explicit protocol policy, not local guesswork or fallback artifacts.

---

## G5: Final output artifact contract

### What it is
Canonical definition of experiment outputs (per-peer, aggregate, or both), format, and interpretation.

### First-principles reason
If outputs are ambiguous, results are not reliably comparable or scientifically interpretable.

### Why current baseline is insufficient
Lifecycle may emit outputs successfully, but unclear canonical artifact policy creates ambiguity for parity comparison.

### Behavioral parity requirement
Define and enforce one stable output contract for MQ runs, including naming, ownership, aggregation semantics, and comparison expectations.

---

## Summary

- J1-J14 proves: **MQ runtime can run**.
- G1-G5 prove: **MQ runtime means the same thing as Abstract QUANTAS**.

Behavioral parity is reached only when all five gaps are closed with validation evidence.
