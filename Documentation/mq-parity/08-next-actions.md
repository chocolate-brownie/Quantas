# Next Actions (Active Queue)

## Completed
1. M1 topology parity
- MQ now builds local assignments from topology rules instead of the all-neighbor fallback.
- `complete`, `ring`, `grid`, and `userList` are covered by `TopologyParityInput.json`.
- Evidence is recorded in `06-validation-matrix.md`.

## Now
1. M2 distribution/channel parity implementation kickoff
- map README distribution semantics to MQ transport behavior
- define validation inputs for drop/reorder/duplicate/delay behavior
- update `06-validation-matrix.md` with scenario evidence

## After M2
- M3 `tests > 1` parity kickoff.
