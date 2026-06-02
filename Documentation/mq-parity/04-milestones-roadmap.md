# Milestones Roadmap

## M1 - Topology parity
Status:
- PASS. Evidence recorded in `06-validation-matrix.md`.

Objective:
- Implement topology-faithful assignment/wiring in MQ runtime.

Exit criteria:
- Neighbor sets and behavior match intended topology for at least: `complete`, `ring`, `grid`, `userList`.

Validation:
- Build: `make -j4 mq_peer_debug mq_leader_debug`
- Runtime: `make mq_run_all INPUTFILE=<topology_input.json> MQ_TOTAL_PEERS=<N> MQ_ROUNDS=<R>`

## M2 - Distribution/channel parity
Objective:
- Map QUANTAS distribution model semantics into MQ runtime behavior.

Exit criteria:
- Controlled runs show expected drop/reorder/duplicate/delay behavior envelope.

Validation:
- Scenario-specific inputs + evidence logs in `06-validation-matrix.md`.

## M3 - `tests > 1` parity
Objective:
- Support repeated tests semantics and reporting in MQ worker lifecycle.

Exit criteria:
- Multiple test repeats execute and produce consistent structured outputs.

## M4 - Stop-policy finalization
Objective:
- Finalize `DoneSignals` behavior to remove fallback ambiguity and guarantee protocol-driven stop.

Exit criteria:
- Deterministic stop behavior with stable logs and clean exits.

## M5 - Output artifact contract finalization
Objective:
- Finalize canonical output contract across MQ runs.

Exit criteria:
- Deterministic, non-corrupted artifacts suitable for backend comparison.
