# Issue #56 comparison result

## Conclusion

For the supported StableDataLink experiment, Abstract did not approximate the
wall-clock runtime of BoostMQ. The closest Abstract setting was
`maxDelay = 5`, but it differed by `95.94%`, so it was not within the target
5%.

This result is expected to be interpreted as a timing-model finding, not an
algorithm-correctness failure. Abstract advances a logical round-based model;
BoostMQ measures real process startup, scheduling, and local message-queue
communication.

## Experiment

- Algorithm: `StableDataLinkPeer`
- Peers: 2
- Topology: complete
- Tests per experiment: 5
- Rounds per test: 100
- Distribution: uniform, no simulated drops
- Metric: total runtime in seconds
- Formula: `abs(Abstract RunTime - BoostMQ durationSeconds) / BoostMQ durationSeconds * 100`
- Commands:
  - `make run INPUTFILE=docs/comparisons/issue-56/stable-datalink/comparison.json`
  - `make mq INPUTFILE=docs/comparisons/issue-56/stable-datalink/comparison.json`
  - `python3 docs/comparisons/issue-56/compare_timings.py docs/comparisons/issue-56/stable-datalink/manifest.json --output docs/comparisons/issue-56/stable-datalink/results.json`

## Measurements

| Abstract maxDelay | Abstract seconds | BoostMQ seconds | Difference |
| ---: | ---: | ---: | ---: |
| 1 | 0.009512 | 0.276008 | 96.55% |
| 2 | 0.011045 | 0.279802 | 96.05% |
| 3 | 0.006935 | 0.271117 | 97.44% |
| 4 | 0.005256 | 0.253484 | 97.93% |
| 5 | 0.011066 | 0.272526 | 95.94% |

Every BoostMQ experiment completed all five tests with both peers and the
leader reporting success. No backpressure drops occurred. Pending shutdown
messages are reported separately by BoostMQ and are not treated as packet
loss.

## Reproducibility

The input, manifest, calculation script, Abstract JSON outputs, and BoostMQ
leader/peer reports are stored in the `stable-datalink/` directory. The calculation can
be rerun from the saved raw files without rerunning either backend.

Environment: Fedora Linux kernel `7.1.8-200.fc44.x86_64`, GCC `16.2.1`, Boost
`1.90.0`.

After the BoostMQ run, `make mq_status` reported no QUANTAS processes and no
QUANTAS resources in `/dev/shm`. `git diff --check` passed.

## Limitations

The two runtimes do not measure the same physical work. Abstract delay values
control logical message availability in rounds, while BoostMQ includes process
creation, OS scheduling, queue operations, and shutdown coordination. The
comparison therefore does not establish a conversion from Abstract delay to
real network latency.
