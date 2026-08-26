# Issue #56: Abstract vs BoostMQ timing comparison

This comparison uses the supported `StableDataLinkPeer` algorithm with two
peers, a complete static topology, five tests, and 100 rounds per test. The
same JSON is used by both backends. Abstract receives `maxDelay` values from 1
through 5; BoostMQ uses real local processes and message queues, so it ignores
the Abstract-only delay model.

## Reproduce

From the repository root:

```sh
mkdir -p docs/comparisons/issue-56/raw/abstract docs/comparisons/issue-56/raw/boostmq
make run INPUTFILE=docs/comparisons/issue-56/stable-datalink-comparison.json
make mq INPUTFILE=docs/comparisons/issue-56/stable-datalink-comparison.json
```

The Abstract run writes five files under `raw/abstract/`. BoostMQ writes five
leader reports under `results/stable-delay-N_EXP<N>/`; copy each
`leader_report.json` into the matching `raw/boostmq/` path before calculating.

Create a manifest with one row per delay:

```json
[
  {"delay": 1, "abstract": "raw/abstract/stable-delay-1.json", "boostmq": "raw/boostmq/stable-delay-1.json"}
]
```

Add rows for delays 2 through 5, then run:

```sh
python3 docs/comparisons/issue-56/compare_timings.py manifest.json \
  --output docs/comparisons/issue-56/comparison-results.json
```

The percentage is:

```text
abs(Abstract RunTime - BoostMQ durationSeconds) / BoostMQ durationSeconds * 100
```

`RunTime` and `durationSeconds` are total framework runtime values. They are
not proof that message ordering, queue pressure, or algorithm correctness is
equivalent. BoostMQ scheduling and IPC timing can vary between runs.
