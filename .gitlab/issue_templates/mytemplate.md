## Summary
[One paragraph: what you will implement and which parity gap it closes.]

## Motivation
[Why this is needed now. Explain why operational baseline is not enough and what behavioral mismatch this issue fixes.]

## Current Behavior
- MQ currently does: [current behavior]
- Divergence from Abstract: [exact mismatch]
- Affected areas/files:
  - `[path/to/file1]`
  - `[path/to/file2]`
  - `[path/to/file3]`

## Target Behavior
For identical input config, MQ must:
- [target semantic property 1]
- [target semantic property 2]
- [target semantic property 3]

## Scope

### In Scope
- [specific module/function change 1]
- [specific module/function change 2]
- [tests/docs updates]

### Out of Scope
- [explicitly excluded item 1]
- [explicitly excluded item 2]
- [future milestone item not part of this issue]

## Implementation Notes (Coding Map)
- Primary files:
  - `[path/to/primary1]`
  - `[path/to/primary2]`
- Supporting files:
  - `[path/to/support1]`
  - `[path/to/support2]`
- New helper module (if needed):
  - `[path/to/new-helper]`
- Guardrail:
  - [what must NOT change outside this issue]

## Acceptance Criteria
- [ ] Given [scenario A], MQ produces [expected semantic result]
- [ ] Given [scenario B], MQ produces [expected semantic result]
- [ ] Given [scenario C], MQ produces [expected semantic result]
- [ ] No regression in [existing lifecycle behavior]
- [ ] Validation evidence added to `Documentation/mq-parity/06-validation-matrix.md`
- [ ] `Documentation/mq-parity/08-next-actions.md` updated accordingly

## Validation Plan
Run:
- `[command 1]`
- `[command 2]`
- `[command 3]`

Evidence required:
- [artifact/log proving criterion 1]
- [artifact/log proving criterion 2]
- [artifact/log proving criterion 3]
- [comparison note against Abstract expectation]

## Risks / Edge Cases
- [edge case 1]
- [edge case 2]
- [ordering/timing/concurrency caveat]
- [performance caveat]

## Definition of Done
This issue is done when:
1. all acceptance criteria are checked,
2. evidence is recorded in `Documentation/mq-parity/06-validation-matrix.md`,
3. PR description maps each criterion to code changes and proof.
