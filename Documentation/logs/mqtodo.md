# Remaining might todos

### Add real IPC countes

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

# TODO for the next phase 

**Experiment Goal**

The goal is to understand whether the abstract simulation model can approximate the real BoostMQ execution model closely enough to be useful.

More specifically, we want to check whether different random-delay settings in the abstract model can produce results that match the BoostMQ backend within an acceptable margin, for example within 5%.

**Main Question**

Can we tune the abstract model’s delay parameters so that its behavior becomes comparable to the real message queue implementation?

**Planned Experiment**

Run the same experiments using both backends:

1. Abstract simulation backend.
2. BoostMQ/message queue backend.

For each example algorithm, run the same configuration under both settings and compare the results.

**Comparison Method**

Use the current configuration files as the starting point.

For the abstract backend, vary the average/random delay settings from `1` to `5`.

For the BoostMQ backend, measure real execution time.

Then compare the abstract results against the BoostMQ results to see which delay setting gives the closest match.

**Purpose**

The purpose is to get a practical sense of whether the abstract model is realistic.

If the abstract model can match BoostMQ results within roughly 5%, then it may be useful for faster experimentation.

If the results differ significantly, then the abstract backend may need better delay modeling, or we need to clearly document that it is only a logical simulator and not a realistic timing model.

**Refined Notes**

We want to run equivalent experiments in both the abstract backend and the BoostMQ backend. The objective is to determine whether the random delay parameters in the abstract model can be tuned so that the abstract execution time approximates the real BoostMQ execution time within about 5%.

Using the current configuration files, we should run all available examples with both backends, collect timing results, and compare them. For the abstract backend, we will vary the average delay from 1 to 5 and observe which setting most closely matches the measured real time of the BoostMQ backend.

The larger research question is whether the abstract simulation model is realistic enough for performance-oriented experiments, or whether it should only be used for logical correctness and algorithm behavior.