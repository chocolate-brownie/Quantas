# ConcreteMQ v1 Minimal TODO

Goal: finish ConcreteMQ v1 as a dependable makefile-orchestrated real IPC
backend for running QUANTAS algorithms, then move on to ZeroMQ.

## Architectural Decisions

Keep these fixed while coding:
- ConcreteMQ is a realistic IPC/process backend, not an Abstract simulator clone.
- Do not implement Abstract channel semantics in ConcreteMQ v1:
  - no configured delay;
  - no model drop;
  - no duplicate;
  - no reorder;
  - no `maxMsgsRec`;
  - no Abstract channel-size behavior.
- Keep process orchestration in the root `makefile`.
- Do not build a C++ process manager for v1.
- Keep per-peer output files as debugging artifacts.
- Make the leader responsible for the researcher-facing experiment report.
- Support JSON `tests > 1`; otherwise ConcreteMQ is not dependable enough for
  researchers.

## Work Items

### 1. Implement repeated JSON tests

Why:
- Abstract QUANTAS repeats experiments with `tests`.
- Researchers need repeated runs, not one IPC demo run.

Do:
- Add a test loop around the current MQ experiment lifecycle.
- For each test, run fresh readiness, assignment, start, rounds, done, stop, and
  cleanup.
- Reset peer-local state, `RoundManager`, interfaces, and output selection per
  test.
- Include experiment index, test index, and peer id in output names.

Verify:
```sh
make -j4 mq_peer_debug mq_leader_debug
make mq_run_all INPUTFILE=quantas/AltBitPeer/AltBitUtility.json MQ_TOTAL_PEERS=2
```

Done when:
- [x] `tests = 1` still works.
- [x] `tests > 1` runs the expected number of test iterations.
- [x] Logs clearly show experiment/test boundaries.
- [x] No stale MQ queues or peer state leak between tests.

### 2. Add leader-owned experiment report

Why:
- Peer files are useful for debugging, but the leader is the only process with a
  global view of the run.
- Researchers need one trusted artifact that says what ran and whether it
  completed.

Do:
- Make the leader write a report per run, experiment, or test.
- Keep the first report simple.
- Include:
  - input/config name;
  - experiment index;
  - test index;
  - peer count;
  - peer type;
  - topology type;
  - round count;
  - peer completion status;
  - per-peer output paths;
  - final success/failure status.

Verify:
```sh
make -j4 mq_peer_debug mq_leader_debug
make mq_run_all INPUTFILE=quantas/BitcoinPeer/Bitcoin3PeerMQDemo.json MQ_TOTAL_PEERS=3
```

Done when:
- [ ] Leader report exists.
- [ ] Report lists all expected peers.
- [ ] Report references per-peer debug output files.
- [ ] Report makes failed or missing peers visible.

### 3. Add real IPC counters

Why:
- ConcreteMQ is real IPC, so the important evidence is send, receive, delivery,
  latency, and backpressure behavior.
- Do not mix real MQ backpressure with Abstract model drops.

Do:
- Track only these v1 counters:
```text
sent
received_raw
delivered_to_instream
dropped_backpressure
```
- Rename ambiguous `dropped=` logs to `dropped_backpressure=`.
- Do not add `dropped_model`.
- Do not add a pending-delivery buffer unless the counter implementation becomes
  messy without it.

Verify:
```sh
make -j4 mq_peer_debug mq_leader_debug
make mq_run_all INPUTFILE=quantas/ExamplePeer/TopologyParityInput.json MQ_TOTAL_PEERS=4 MQ_ROUNDS=1
make mq_run_all INPUTFILE=quantas/BitcoinPeer/Bitcoin3PeerMQDemo.json MQ_TOTAL_PEERS=3
```

Done when:
- [ ] Sends increment `sent`.
- [ ] MQ receives increment `received_raw`.
- [ ] Packets pushed to `_inStream` increment `delivered_to_instream`.
- [ ] Send timeouts increment `dropped_backpressure`.
- [ ] Logs or reports show per-peer MQ stats.

### 4. Run final ConcreteMQ v1 validation

Why:
- This gives a stopping condition so ConcreteMQ does not become an endless
  parity project.

Run:
```sh
make clean
make -j4 mq_peer_debug mq_leader_debug
make mq_run_all INPUTFILE=quantas/ExamplePeer/TopologyParityInput.json MQ_TOTAL_PEERS=4 MQ_ROUNDS=1
make mq_run_all INPUTFILE=quantas/BitcoinPeer/Bitcoin3PeerMQDemo.json MQ_TOTAL_PEERS=3
make mq_run_all INPUTFILE=quantas/AltBitPeer/AltBitUtility.json MQ_TOTAL_PEERS=2
```

Save evidence under:
```text
Documentation/experiments/<date>-concretemq-v1-validation/
```

Done when:
- [ ] Leader and all peers exit with code `0`.
- [ ] Repeated tests are enabled.
- [ ] Leader report exists.
- [ ] Per-peer debug files exist.
- [ ] Logs show topology assignment.
- [ ] Logs or reports show IPC counters.
- [ ] Docs clearly state ConcreteMQ v1 non-goals:
  - no Abstract channel semantics;
  - no global lockstep barrier;
  - no C++ process manager.

## Next Action

Build the leader-owned experiment report next. Repeated JSON tests are now
working, so the next dependable-researcher milestone is one trusted global run
artifact from the leader.
