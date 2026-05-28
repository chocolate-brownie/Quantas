# Execution Playbook

## Issue-first template (short)
- Objective
- Expected invariant
- Scope in / out
- Validation commands
- Risks

## Branching
- One milestone per branch.
- Recommended naming:
  - `feat/mq-m1-topology-parity`
  - `feat/mq-m2-distribution-parity`

## Commit strategy
- Prefer small commits by invariant:
  - parsing/config
  - runtime protocol
  - output/reporting
  - validation/docs

## Validation rule
- No milestone close without:
  - build pass
  - runtime pass
  - explicit evidence entry in `06-validation-matrix.md`

## Daily discipline
- Update `Documentation/logs/daily-log.md` with a short technical summary.
