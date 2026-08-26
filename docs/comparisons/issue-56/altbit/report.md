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
- Calculation command: `python3 docs/comparisons/issue-56/compare_timings.py docs/comparisons/issue-56/altbit/manifest.json --output docs/comparisons/issue-56/altbit/results.json`

## Measurements

| Abstract maxDelay | Abstract seconds | BoostMQ seconds | Difference |
| ---: | ---: | ---: | ---: |
| 1 | 0.005048 | 0.275056 | 98.16% |
| 2 | 0.006292 | 0.253918 | 97.52% |
| 3 | 0.008626 | 0.280751 | 96.93% |
| 4 | 0.008402 | 0.286183 | 97.06% |
| 5 | 0.008761 | 0.268948 | 96.74% |

The closest Abstract setting was `maxDelay = 5`, with a `96.74%` difference.
All five BoostMQ experiments completed all five tests with both peers and the
leader reporting success. No backpressure drops occurred.

## Transport evidence

The final test in each delay run reported the delivery totals below. Peak queue
usage is the maximum observed across the complete five-test run:

| Delay | Sent | Delivered | Pending at shutdown | Backpressure drops | Peak queue usage |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 111 | 99 | 12 | 0 | 9 |
| 2 | 115 | 95 | 20 | 0 | 3 |
| 3 | 117 | 102 | 15 | 0 | 2 |
| 4 | 116 | 100 | 16 | 0 | 1 |
| 5 | 114 | 95 | 19 | 0 | 2 |

`reliable: true` means that no messages were rejected by a full queue. It does
not mean that every sent message had been delivered before fixed-round
shutdown. The pending counts are therefore evidence for the queue-draining
follow-up, not evidence that AltBit lost messages.

Per-message latency, throughput, and a cross-backend final-state comparison
were not collected by this timing fixture; they remain explicit follow-up
measurements rather than being inferred from total runtime.

The configuration, manifest, Abstract outputs, BoostMQ leader reports, and
peer reports are stored in the `altbit/` directory beside this report. The result is not a claim that AltBit
is incorrect; it shows that Abstract logical-round timing and BoostMQ real
process/IPC timing are different measurements.
