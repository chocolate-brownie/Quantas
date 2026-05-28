# MQ Parity Contract (First Principles)

To claim that MQ is a fully functioning QUANTAS network/runtime layer, these invariants must be true:

1. Input semantics parity
- Same JSON meaning for `topology`, `distribution`, `tests`, `rounds`, `parameters`, and `logFile`.

2. Lifecycle parity
- Same experiment lifecycle boundary behavior:
  - configure
  - assign
  - build
  - initialize
  - start synchronization
  - round execution
  - round/experiment hooks
  - stop handshake
  - output emission
  - cleanup

3. Termination parity
- Stop is protocol-authoritative and deterministic (not local guesswork).

4. Output parity
- Final artifacts are valid, stable, and match agreed QUANTAS reporting semantics.

5. Isolation parity
- No cross-process artifact corruption and no stale experiment state leakage.

## Current verdict

- MQ execution backbone: working.
- Full README semantics parity: not complete yet.
