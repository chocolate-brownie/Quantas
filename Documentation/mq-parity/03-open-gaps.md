# Open Gaps (Parity-Critical)

## G1 - Topology semantics parity
- Current MQ assignment path is still phase-1 simplified and not fully topology-faithful.
- Must support README topology contract (`complete`, `star`, `grid`, `torus`, `chain`, `ring`, `unidirectionalRing`, `userList`).

## G2 - Distribution/channel semantics parity
- README channel behavior (`dropProbability`, `duplicateProbability`, `reorderProbability`, delay model semantics, queue behavior) is not fully mapped to MQ transport behavior yet.

## G3 - `tests > 1` semantics parity
- Current MQ path is effectively single-test oriented with warning behavior.
- Must run and report repeated tests per experiment.

## G4 - Stop-policy hardening
- Baseline stop handshake is wired.
- `DoneSignals` semantics still need final policy cleanup and explicit behavior guarantees.

## G5 - Output artifact contract
- Decide and enforce canonical MQ artifact policy:
  - per-peer files,
  - aggregate file,
  - or both.
