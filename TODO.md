# BoostMQ Compatibility TODO

## Goal

A researcher should write one algorithm `.cpp` file and run it with either
Abstract or BoostMQ.

We use three groups:

- **P — Same:** both backends must give the algorithm the same logical behavior.
- **D — Different:** BoostMQ works differently because it uses real processes.
  We accept, measure, and explain these differences.
- **U — Unsupported:** BoostMQ cannot support these actions across separate
  processes. We must explain them and stop unsupported runs early when possible.

`Yes`, `Partial`, and `No` tell us what the code can do today. They do not move a
task from P to D or U.

## First: finish issue #40

Issue #40 only decides what QUANTAS will promise. It does not build every missing
feature.

- [x] Confirm that researchers must not change their algorithm source code when
      switching between Abstract and BoostMQ.
- [x] Agree on the P, D, and U lists below.
- [x] Write `Yes`, `Partial`, or `No` beside every P item.
- [x] Give every unfinished P item to an existing issue or a new issue.
- [x] Write down any concerns raised during review.
- [x] Update #41, #42, and parent issue #25 to match the final decision.
- [x] Close #40 after the decision is approved.

### Review concerns

- Same source code does not prove the same behavior; compare the results.
- A BoostMQ process cannot use live `Peer*` objects from another process.
- The framework must choose roles and global actions once for the whole run.
- Real-process differences must not change the logical experiment.
- Only static topology is supported; reject live topology changes.
- Comparison runs need repeatable seeds and complete peer output.

## P — Behavior that must be the same

### Check on the current branch

Test these on the current #40 branch.

- [x] **Yes** — P1: both backends compile the same researcher algorithm source.
- [x] **Yes** — P2: both backends create the same peer type through `PeerRegistry`.
- [x] **Yes** — P3: both backends use the same set of peer IDs.
- [ ] **Partial** — P5: both backends give peers the same shared experiment settings. See #51.
- [x] **Yes** — P8: both backends call the same `performComputation()` code.
- [x] **Yes** — P9: BoostMQ keeps the packet source, destination, and JSON message unchanged.
- [x] Run at least one algorithm successfully with both backends.
- [ ] Make the tests fail if any item above becomes different.

### New tasks

- [ ] **Partial** — P4 and P10: same peer, test, and round counts. See #50.
- [ ] **No** — P14: same random-seed rule. See #53.
- [ ] **Partial** — P6: same starting topology. See #52 after #53.

### Existing issues that already cover unfinished P items

Do not open new issues for these tasks.

- [ ] **Partial** — P7: same local starting state. See #41.
- [ ] **No** — P11: same hook calls. See #41.
- [ ] **No** — P12: same Byzantine, crashed, and miner choices. See #41.
- [ ] **No** — P13: one global action when requested. See #41.
- [ ] **Partial** — P15: same per-peer metric meaning. See #42.
- [ ] **No** — P16: same whole-experiment metric meaning. See #42.
- [ ] **Partial** — P17: clean state between tests. See #27.
- [ ] **No** — P18: same final success or failure result. See #45, #39, and #42.

### P is finished when

- [ ] Every P item says `Yes`.
- [ ] Every P item has an automatic test or a saved test run that proves it.
- [ ] Researcher algorithms contain no BoostMQ-only code.
- [ ] Exit code `0` alone is never treated as proof that both backends behaved the
      same.

## D — Real-world behavior that may be different

BoostMQ must handle and report these things correctly. It does not need to copy
the Abstract simulator.

- [ ] Total real running time.
- [ ] The order in which the OS runs processes.
- [ ] Real message delay.
- [ ] The order in which messages arrive.
- [ ] Full queues and failed or delayed sends.
- [ ] CPU and memory use.
- [ ] The real time when each peer finishes a round.
- [ ] The time needed to notice a failure.
- [ ] The order in which process output appears.

Issue #40 decides these limits. Issue #43 explains them and saves the machine,
timing, resource, queue, and message results from the final tests.

### D is finished when

- [ ] Every difference has a clear name, unit, and place in the report.
- [ ] The documentation explains why it is different.
- [ ] These real-world differences do not silently change peer count, IDs,
      topology, settings, roles, actions, rounds, tests, or the final logical
      result.

## U — Behavior that BoostMQ cannot support

- [ ] A `vector<Peer*>` containing real peer objects from other processes.
- [ ] Reading another peer's private fields directly.
- [ ] Calling any method directly on a remote `Peer*`.
- [ ] Replacing a remote peer object with pointer assignment.
- [ ] Sharing normal C++ pointers or references between processes.
- [ ] Assuming every peer runs in one shared memory space at the same speed.
- [ ] Treating Abstract delay, drop, duplicate, reorder, or `maxMsgsRec` settings
      as real BoostMQ behavior.
- [ ] Changing the topology after the run starts.
- [ ] Changing global algorithm data in a way that cannot be sent as IDs,
      settings, messages, one planned action, or collected results.

Issue #40 decides this list. Issue #41 adds early checks where possible. Issue
#43 explains the limits and shows supported alternatives.

### U is finished when

- [ ] Each unsupported action or setting is clearly listed.
- [ ] QUANTAS stops before starting when it can detect an unsupported request.
- [ ] The error says what is unsupported and what the researcher can do instead.
- [ ] QUANTAS never pretends that a local `Peer*` points to a live peer in another
      process.

## Work order

Keep the issue order in parent issue #25. Add the new tasks at these points:

1. Finish #40.
2. Finish #50, #51, and #53.
3. Finish #52 after #53.
4. Finish #32, #45, #31, #33, #28, and #39.
5. Finish #27.
6. Finish #41, then #42.
7. Finish #43 and prepare the project for researchers.
8. Start the Abstract versus BoostMQ comparison study only after #43 passes.
