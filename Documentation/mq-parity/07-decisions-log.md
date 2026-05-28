# Decisions Log

## D1 - Per-peer output file disambiguation
- Decision: write MQ worker outputs with peer suffix (`..._p<peerId>`) to avoid concurrent write corruption.
- Rationale: multiple workers append/write during same experiment lifecycle.
- Revisit condition: if aggregate writer/orchestrated merge is introduced.

## D2 - Baseline-first strategy
- Decision: complete lifecycle baseline first (J1-J14), then semantic parity.
- Rationale: stabilize runtime plumbing before strict behavior matching.

## D3 - MQ scope separation from TCP
- Decision: MQ parity is tracked independently from TCP branch progress.
- Rationale: avoids mixed assumptions and preserves milestone clarity.
