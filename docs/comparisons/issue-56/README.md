# Issue #56: Abstract vs BoostMQ timing comparison

This directory contains two independent, reproducible experiments. Each
algorithm has its own folder so configurations, manifests, results, and raw
backend output cannot be confused or overwritten.

```text
issue-56/
├── compare_timings.py
├── stable-datalink/
│   ├── comparison.json
│   ├── manifest.json
│   ├── results.json
│   ├── report.md
│   └── raw/{abstract,boostmq}/
└── altbit/
    ├── comparison.json
    ├── manifest.json
    ├── results.json
    ├── report.md
    └── raw/{abstract,boostmq}/
```

Both experiments use two peers, a complete static topology, five tests, and
100 rounds per test. Abstract receives `maxDelay` values from 1 through 5;
BoostMQ uses real local processes and message queues, so it does not use the
Abstract-only delay model.

## Reproduce StableDataLink

From the repository root:

```sh
mkdir -p docs/comparisons/issue-56/stable-datalink/raw/abstract docs/comparisons/issue-56/stable-datalink/raw/boostmq
make run INPUTFILE=docs/comparisons/issue-56/stable-datalink/comparison.json
make mq INPUTFILE=docs/comparisons/issue-56/stable-datalink/comparison.json
```

The Abstract run writes `stable-datalink-delay-1.json` through
`stable-datalink-delay-5.json` under `stable-datalink/raw/abstract/`.
BoostMQ writes matching directories under `results/`; copy each complete
directory into `stable-datalink/raw/boostmq/`.

```sh
python3 docs/comparisons/issue-56/compare_timings.py \
  docs/comparisons/issue-56/stable-datalink/manifest.json \
  --output docs/comparisons/issue-56/stable-datalink/results.json
```

## Reproduce AltBit

```sh
mkdir -p docs/comparisons/issue-56/altbit/raw/abstract docs/comparisons/issue-56/altbit/raw/boostmq
make run INPUTFILE=docs/comparisons/issue-56/altbit/comparison.json
make mq INPUTFILE=docs/comparisons/issue-56/altbit/comparison.json
python3 docs/comparisons/issue-56/compare_timings.py \
  docs/comparisons/issue-56/altbit/manifest.json \
  --output docs/comparisons/issue-56/altbit/results.json
```

Copy the five `altbit-delay-N_EXPN` BoostMQ result directories into
`altbit/raw/boostmq/` before calculating. The manifests point to each
algorithm's complete raw report set; the calculation can therefore be rerun
without rerunning either backend.

The percentage is:

```text
abs(Abstract RunTime - BoostMQ durationSeconds) / BoostMQ durationSeconds * 100
```

`RunTime` and `durationSeconds` are total framework runtime values. They are
not proof that message ordering, queue pressure, or algorithm correctness is
equivalent. BoostMQ scheduling and IPC timing can vary between runs.
