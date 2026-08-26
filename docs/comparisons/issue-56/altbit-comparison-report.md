# Issue #56 AltBit comparison result

AltBit confirms the StableDataLink result: Abstract delay values do not
approximate BoostMQ wall-clock runtime for this experiment.

## Experiment

- Algorithm: existing `AltBitPeer`
- Peers: 2
- Topology: complete
- Tests per experiment: 5
- Rounds per test: 100
- Parameters: `timeOutRate = 2`
- Distribution: uniform, no simulated drops
- Metric: total runtime in seconds
- Formula: `abs(Abstract RunTime - BoostMQ durationSeconds) / BoostMQ durationSeconds * 100`
- Calculation command: `python3 docs/comparisons/issue-56/compare_timings.py docs/comparisons/issue-56/altbit-manifest.json --output docs/comparisons/issue-56/altbit-comparison-results.json`

## Measurements

| Abstract maxDelay | Abstract seconds | BoostMQ seconds | Difference |
| ---: | ---: | ---: | ---: |
| 1 | 0.006601 | 0.278171 | 97.63% |
| 2 | 0.010975 | 0.273265 | 95.98% |
| 3 | 0.007841 | 0.267166 | 97.07% |
| 4 | 0.007799 | 0.260556 | 97.01% |
| 5 | 0.010514 | 0.276189 | 96.19% |

The closest Abstract setting was `maxDelay = 2`, with a `95.98%` difference.
All five BoostMQ experiments completed all five tests with both peers and the
leader reporting success. No backpressure drops occurred.

The raw configurations, Abstract outputs, BoostMQ leader reports, and peer
reports are stored beside this report. The result is not a claim that AltBit
is incorrect; it shows that Abstract logical-round timing and BoostMQ real
process/IPC timing are different measurements.
