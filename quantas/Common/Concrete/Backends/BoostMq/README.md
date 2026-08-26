# Communication backend using C++ Boost Message Queues

[Boost.Interprocess Documentation](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html)

## Purpose

### What are Boost message queues used for?

BoostMQ is an experimental multi-process communication backend for QUANTAS. The current implementation lives under `quantas/Common/Concrete/Backends/BoostMq/` and uses Boost message queues for local IPC.

BoostMQ is a concrete local IPC backend. It is used to check whether QUANTAS algorithms can run as separate OS processes while keeping the normal `Peer` / `NetworkInterface` API.

Use it when you want evidence that a supported algorithm can run outside the
single-process Abstract simulator. BoostMQ is added alongside Abstract; it does
not redesign or replace the existing simulator.

The input JSON still controls:

- algorithms
- experiments
- `topology.initialPeers`
- `topology.initialPeerType`
- rounds
- tests

### What it is not

BoostMQ does not simulate Abstract channel delay, drop, duplicate, reorder, or maxMsgsRec behavior. Those are Abstract simulator controls. BoostMQ uses real OS message queues and real process scheduling.

## Source Layout

```bash
quantas/Common/Concrete/
  Runtime/
    Config/        shared experiment config parsing
    Topology/      shared peer assignment and topology planning
  Backends/
    BoostMq/
      Entrypoints/ peer and leader executables
      Control/     ready/assignment/start/done/stop protocol
      Transport/   Boost message queue network interface 
    TCP/           older TCP concrete backend
    ZeroMq/        scaffold for the future ZeroMQ backend
```

## Usage and instructions

Run `make help` for Makefile instruction if necessary

Install the Boost development package used by this backend before building on Ubuntu or Linux Mint:

```sh
sudo apt install libboost-serialization-dev
```

You can verify the local dependency setup with:

```sh
make check_mq_deps
```

Run the full BoostMQ workflow from the repository root with:

```sh
make mq INPUTFILE=quantas/ExamplePeer/ExampleInput.json
```

This builds and runs the two BoostMQ binaries: `quantas_mq_leader.exe` and `quantas_mq_peer.exe`. The leader coordinates the experiment lifecycle and writes the leader report. The peer binary is launched once per peer and executes that peer's assigned rounds.

Use the JSON file as the only source for peer, test, and round counts. To change
an experiment, edit its `topology.initialPeers`, `tests`, or `rounds` value and
run `make mq` again. Runtime count overrides are not supported.

The JSON `parameters` object reaches researcher algorithms through the same
`Peer::initParameters(const std::vector<Peer*>&, json)` hook in Abstract and
BoostMQ. Algorithm declarations must use that exact signature with `override`.
When `parameters` is missing, both backends pass an empty JSON object.

If one JSON file contains experiments with different peer counts, BoostMQ runs
them separately and starts the correct peers for each experiment.

Before starting any peer processes, `make mq` uses the leader's preflight mode
to validate every experiment in the JSON. Preflight checks the configuration
and queue requirements without running the experiments. If any experiment is
invalid, the command stops and no experiment is launched.

After preflight succeeds, `make mq` runs every experiment in the JSON in order.
Each experiment gets a fresh leader process and the number of fresh peer
processes set by that experiment's `topology.initialPeers` value. The next
experiment starts only after the current one finishes.

When an experiment has more than one test, each test also starts with fresh
peer objects, transport counters, completion tracking, and control/data queues.
Each test writes separate peer output files and one matching leader report
entry. If a test fails, the experiment stops; a later test cannot be reported
as successful using state from the failed test.

Run the same complete workflow with debug binaries and detailed symbols using:

```sh
make mq_debug INPUTFILE=quantas/ExamplePeer/ExampleInput.json
```

The leader sizes the shared `mq_barrier` and `mq_done` queues from the experiment peer count so each peer has room for one ready or done signal. On the supported Linux environment, Boost.Interprocess stores these resources under `/dev/shm`, for example `/dev/shm/mq_barrier` and `/dev/shm/peer_0_data`. They are not governed by the POSIX message-queue setting `fs.mqueue.msg_max`.

### Queue configuration

Each experiment may configure its data queues:

```json
"boostMq": {
  "dataQueueCapacity": 1000,
  "maxMessageSizeBytes": 4096,
  "readyTimeoutMs": 30000,
  "controlSendTimeoutMs": 5000,
  "dataSendTimeoutMs": 5
}
```

All five settings are optional. Their defaults are `1000` messages, `4096` bytes,
`30000` milliseconds for readiness, `5000` milliseconds for required control
sends, and `5` milliseconds for data sends. `readyTimeoutMs` limits how long the
leader waits for every expected peer to report ready. `controlSendTimeoutMs`
limits each assignment, start, and normal stop send. `dataSendTimeoutMs` limits
how long a peer waits to put one algorithm message into a full destination queue.
The control queue capacity is derived from `topology.initialPeers`; it is not a
separate JSON setting. Before launching workers, `make mq` checks that the queues
can be created and that every topology assignment fits within
`maxMessageSizeBytes`.

### Static topologies

BoostMQ supports the same fixed-ID neighbour rules as Abstract for `complete`,
`star`, `grid`, `torus`, `chain`, `ring`, `unidirectionalRing`, and `userList`.
Grid and torus dimensions must be positive and their product must equal
`topology.initialPeers`. A torus needs at least two rows and two columns.
User-list peer IDs and neighbour IDs must be valid integers in the configured
peer range, and a peer cannot list itself.

Invalid topology settings fail during leader preflight before queues or peer
processes start. With `"identifiers": "random"`, identifier order may differ
between Abstract and BoostMQ runs. Live topology changes remain unsupported.

### Result files

BoostMQ stores generated output under `results/`. Each experiment gets one
folder named from its configured `logFile` and experiment number:

```text
results/
  AltBitdropProb0_EXP1/
    leader_report.json
    peer_0_TEST1.txt
    peer_1_TEST1.txt
```

`leader_report.json` is the main experiment report. The peer files contain the
raw output for one peer and one test. The leader report references every raw
file in `peerOutputFiles`. A JSON file containing several experiments creates a
separate folder for each experiment. `make clean_outputs` removes generated
experiment folders and legacy root `_EXP` output files.

### How success is decided

`success: true` means every expected peer became ready, completed every test,
and wrote its required output file. It describes framework completion; it does
not claim that the researcher algorithm is correct or that transport metrics
are perfect.

On a startup timeout, the leader report sets `success` to `false` and records
only the useful facts: `timedOut`, `readyPeers`, and `missingPeers`. Detailed
error explanations remain in `QUANTAS_LOG`. Transport metrics and researcher
algorithm output remain separate evidence.

If a destination data queue stays full until the send deadline, the sender
records one `dropped_backpressure`, stops the current test, and reports failure
to the leader. The leader writes `success: false`, keeps the failed peer's
transport counters in `transportReliability`, stops the other peers, and exits
with a nonzero status. This is a real BoostMQ delivery failure, not simulated
Abstract packet loss.

After a startup timeout, the leader asks peers that already started to stop,
removes the QUANTAS BoostMQ queues, writes the leader report, and exits with a
nonzero status. The launcher then terminates any remaining child processes.

## Crash diagnosis and recovery

An interrupted or crashed run can leave BoostMQ processes or queue resources
behind. Before starting another experiment, inspect the current state:

```sh
make mq_status
```

This command is read-only. It reports active `quantas_mq_leader.exe` and
`quantas_mq_peer.exe` processes and the following QUANTAS resources under
`/dev/shm`:

- `mq_barrier`
- `mq_done`
- `peer_<id>_control`
- `peer_<id>_data`

To recover after an interrupted run:

1. Run `make mq_status`.
2. If leader or peer processes are listed, stop them normally in their terminals.
   For a detached process, use the reported PID with `kill <pid>`.
3. Run `make mq_status` again and confirm that no QUANTAS MQ processes remain.
4. Remove abandoned resources with `make mq_cleanup`.
5. Run `make mq_status` once more before restarting the experiment.

`make mq_cleanup` does not terminate processes. It refuses to remove resources
while a matching leader or peer process is active because deleting an in-use
queue could corrupt a running experiment.

Cleanup output has the following meaning:

- `Removed <path>`: the abandoned QUANTAS resource was removed.
- `No abandoned QUANTAS BoostMQ resources found.`: nothing needed removal; this
  is a successful result.
- `active QUANTAS BoostMQ processes detected; refusing cleanup`: at least one
  MQ process is still running. Each matching resource is reported as `Skipped`.
  Stop the listed processes and retry.
- `pgrep is required`: install the system process utilities before retrying.
- `BoostMQ resource directory /dev/shm is unavailable`: the expected Linux
  shared-memory filesystem is unavailable and the environment must be fixed.

Running `make mq_cleanup` more than once is safe. It targets only the exact
QUANTAS resource names listed above. Do not use broad commands such as
`rm /dev/shm/*`, because they can remove resources owned by other applications.

## How to interpret report metrics

BoostMQ report metrics describe real IPC behavior, not Abstract simulator channel behavior:

- `sent`: messages successfully placed into destination peer queues.
- `received_raw`: raw messages pulled from a peer queue.
- `delivered_to_instream`: received packets decoded and pushed into the peer input stream.
- `dropped_backpressure`: send attempts dropped because the destination queue did not accept the message before the send timeout.
- `data_queue_capacity`: configured maximum number of messages in the peer data queue.
- `peak_observed_queue_usage`: largest number of waiting messages observed when the peer checked its data queue.

The leader also summarizes these counters in `transportReliability`. Treat `dropped_backpressure_total == 0` and `received_raw_total == delivered_to_instream_total` as the primary transport-health check for the current implementation.

`pending_at_shutdown_total` counts messages that were accepted into queues but
were still waiting when the run ended. It is not a drop. A backpressure drop is
counted only by `dropped_backpressure_total` when a send reaches its deadline.
The `reliable` field is false when a backpressure drop occurred or when a raw
message could not be delivered to the local input stream.

The stricter `sent_total == received_raw_total` check can fail when fixed-round shutdown leaves final-round messages still queued. That is reported as pending-at-shutdown behavior, not automatically as modelled packet loss.

## Common failure messages

- queue creation failed → stale queues or insufficient shared-memory resources
- peer timeout → peer crashed or did not send `done`
- message too large → increase `boostMq.maxMessageSizeBytes` or reduce the payload size
- timed out sending data → the destination data queue stayed full, so the test failed

## Researcher algorithm contract

The handover target is a new researcher algorithm that uses the supported
QUANTAS peer interface. Existing algorithms are validation cases, not the limit
of what researchers may run.

Inside one BoostMQ peer process, an algorithm may use:

- its local peer ID and local state;
- its assigned static neighbours;
- the shared experiment parameters from JSON;
- received messages and the normal QUANTAS send operations; and
- local algorithm output and metrics.

Supported algorithms should use the same source with Abstract or BoostMQ. They
must not require BoostMQ-only branches. However, the peer collection passed to
lifecycle hooks contains process-local peer objects only. It is not a collection
of live objects from every remote process.

For example, this is supported because the peer reads JSON settings, keeps local
state, and sends messages by neighbour ID:

```cpp
void MyPeer::initParameters(const std::vector<Peer*>& localPeers, json parameters) {
    timeout = parameters.value("timeout", 10);
}

void MyPeer::performComputation() {
    for (interfaceId neighbour : neighbors()) {
        unicastTo({{"type", "ping"}}, neighbour);
    }
}
```

The `localPeers` vector normally contains only the peer object owned by the
current BoostMQ process. An algorithm must not loop over that vector expecting
to find every peer in the experiment.

BoostMQ currently supports:

- one local algorithm peer per OS process;
- configured peer IDs, counts, tests, rounds, parameters, and static neighbours;
- local computation and message send/receive;
- per-peer metric files; and
- leader completion and transport reports.

The leader collects and validates raw peer output. Algorithm-specific analysis
is handled separately. A clean exit alone does not prove that an algorithm
produced a correct result.

### Existing algorithm audit

- AltBit and StableDataLink can perform their main work from local state and
  messages. Any calculations needed for the final comparison are handled
  outside the BoostMQ runtime in issue #56.
- ExamplePeer works when `changePeerType` is false. Replacing another peer
  through `peers[1]` is not supported across processes.
- PBFT, Bitcoin, Ethereum, Raft, Kademlia, and LinearChord currently inspect the
  complete peer collection during setup. BoostMQ cannot give those hooks live
  remote peer objects, so their global setup is not equivalent to Abstract.
- Hook code that only updates the local peer is supported. Hook code that reads
  or changes remote peer objects is not supported.

BoostMQ does not reject algorithms by name. General C++ code can hide pointer
use in many ways, so the runtime cannot reliably detect this behaviour before
launch. Researchers must follow the contract above and validate their results.

### Natural differences from Abstract

BoostMQ uses real processes and shared-memory queues. Therefore, it does not
promise the same:

- wall-clock time;
- process scheduling or message arrival order;
- real message delay;
- queue pressure and backpressure;
- round completion time; or
- random-number sequence.

These differences must be measured and explained. They must not be presented as
Abstract simulator behaviour.

### Unsupported process behaviour

BoostMQ cannot provide:

- a `vector<Peer*>` containing live remote peer objects;
- direct reads or method calls on another process's peer object;
- shared ordinary C++ pointers, references, or arbitrary global memory;
- replacement of a remote peer through pointer assignment;
- live topology changes; or
- Abstract delay, drop, duplicate, reorder, `maxMsgsRec`, or global-memory
  lockstep behaviour.

For example, PBFT currently receives a one-peer vector in each BoostMQ process.
Code that builds one global committee by directly inspecting every `Peer*` does
not have the same meaning across process boundaries.

## Validation and handover goal

Before researcher handover, the project will validate clean builds, static
topologies, repeated tests, failures, cleanup, reports, and representative
consensus, blockchain, routing, and data-link algorithms. The guide must then be
repeatable by another user or on a clean supported Linux system.

After the BoostMQ backend passes that validation, the same experiment settings
will be run with Abstract and BoostMQ. Abstract delay values from `1` to `5`
will be compared with real BoostMQ measurements. The result will report which
Abstract setting is closest and the percentage difference; it will not assume
in advance that the difference is within 5%.
