# QUANTAS Engineering Workflow Guide

This document defines the default Git and project-management workflow for QUANTAS as the codebase and research scope grow.

## Why this exists

QUANTAS now mixes protocol/runtime engineering, distributed-system assumptions, and experiment validation.
Without structure, changes become hard to reason about, review, and reproduce.

This workflow optimizes for:
- clear objectives per change,
- reproducible evidence,
- minimal integration risk,
- traceable technical decisions.

## Core principles

1. One objective at a time.
2. Model assumptions before implementation.
3. Small, reviewable diffs.
4. Evidence before conclusions.
5. Separate correctness claims from performance claims.

## 1) Branch strategy

Create one branch per objective.

Naming convention:
- `feat/<area>-<objective>`
- `fix/<area>-<bug>`
- `exp/<topic>-<study>`
- `docs/<topic>`

Examples:
- `feat/concretemq-j10-round-hook`
- `fix/concretemq-start-gate-timeout`
- `exp/byzantine-drop-sensitivity`

Rules:
- Do not develop new features directly on `master`.
- Keep branch scope narrow and measurable.
- Rebase/merge frequently enough to avoid long-lived drift.

## 2) Issue-first execution

Before coding, open a short issue (or note) with:
- objective/question,
- assumptions (timing, fault, topology, stop mode),
- expected property/invariant,
- success criteria,
- validation commands,
- risks.

Use this mini-template:

```md
## Objective

## Assumptions
- Execution model:
- Communication model:
- Fault model:

## Property to preserve/prove
- Safety:
- Liveness/termination:

## Success criteria

## Validation commands

## Risks
```

## 3) Commit hygiene

Use small commits with one concern each.

Preferred format (conventional commit + scope):
- `✨ feat(concretemq): ...`
- `🐛 fix(concretemq): ...`
- `♻️ refactor(common): ...`
- `📝 docs(logs): ...`

Rules:
- Subject in imperative mood.
- Avoid mixing unrelated code + docs + cleanup in one commit.
- If mixed changes are unavoidable, state why in commit/PR body.

## 4) Pull request contract

Every PR should include:
- Problem statement/objective.
- What changed (module-level summary).
- Assumptions/model used.
- Safety/liveness/termination impact.
- Validation steps run.
- Key outputs/log evidence.
- Known limitations and next steps.

Recommended PR checklist:

```md
- [ ] Objective is explicit and bounded
- [ ] Assumptions are documented
- [ ] Build passes for affected targets
- [ ] Relevant runtime/test command executed
- [ ] Evidence attached (logs/metrics)
- [ ] Risks/limitations stated honestly
```

## 5) Definition of Done (DoD)

A task is done only when all relevant items are satisfied:
- Code compiles for impacted targets.
- Reproduction/validation command is documented.
- At least one regression check is executed.
- Lifecycle/observability impact is logged (if runtime behavior changed).
- Docs/notes are updated where design intent changed.

## 6) Two-track parity management for MQ work

When working on ConcreteMQ parity, track progress on two independent tracks:

Track A: Execution parity
- startup barrier,
- round lifecycle,
- stop handshake,
- cleanup/termination semantics.

Track B: Metric parity
- hook behavior (`initParameters`, `endOfRound`, `endOfExperiment`),
- aggregation semantics,
- output parity with abstract runtime.

Rule:
- Do not claim “parity complete” unless both tracks have evidence.

## 7) Session management rhythm

At session start:
- lock one objective,
- define success criteria,
- write a 3-5 step plan.

During session:
- keep changes incremental,
- run smallest useful validation first,
- record observed failures with exact command + context.

At session end:
- summarize what changed,
- summarize validation evidence,
- list open risks,
- define the single best next action.

## 8) Evidence and reproducibility requirements

For every important claim, keep enough data to replay:
- commit hash/branch,
- command used,
- input JSON/config,
- peer count/round count,
- environment assumptions (if relevant),
- output log/metrics file path.

Avoid claims without executable evidence.

## 9) Scope control rules

When complexity rises, reduce risk with explicit scope controls:
- one protocol/lifecycle change per PR when possible,
- one hypothesis per experiment run,
- defer broad refactors until behavior is stable,
- separate debugging instrumentation commits from final cleanup commits.

## 10) Lightweight board structure (recommended)

Organize active work with these columns:
- `Backlog`
- `Ready`
- `In Progress`
- `Needs Evidence`
- `Review`
- `Done`

For MQ parity, tag each item with:
- `exec-parity` or `metric-parity`,
- `fault-model`, `lifecycle`, `observability`, `performance` as needed.

## Quick start checklist

Before starting a new change:
- [ ] Branch created from latest `master`
- [ ] Objective and assumptions written
- [ ] Validation command selected

Before opening PR:
- [ ] Commits are scoped and readable
- [ ] Evidence is attached and reproducible
- [ ] Known limitations are explicit

This guide is a living document. Update it whenever process gaps are discovered.
