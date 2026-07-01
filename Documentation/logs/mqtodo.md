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

- Keep backpressure logs named `dropped_backpressure=`.
- Do not add `dropped_model`.
- Do not add a pending-delivery buffer unless the counter implementation becomes
  messy without it.

Done when:

- [ ] Sends increment `sent`.
- [ ] MQ receives increment `received_raw`.
- [ ] Packets pushed to `_inStream` increment `delivered_to_instream`.
- [x] Send timeout logs use `dropped_backpressure`.
- [ ] Send timeouts increment a reportable `dropped_backpressure` counter.
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

Add the real IPC counters. Repeated tests and leader-owned reports now cover both successful completion and timeout failures with explicit missing-peer evidence.

~~Complete README.md of the boostmq~~
~~Test and finalize~~

the file `BitcoinSpeedTest.json` fails due to the assignment payload for a 300-peer complete topology is too large for the app-level queue size.
