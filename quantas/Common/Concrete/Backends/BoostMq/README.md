# Communication backend using C++ Boost Message Queues

[Boost.Interprocess Documentation](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html)

## Purpose

### What are Boost message queues used for?

BoostMQ is an experimental multi-process communication backend for QUANTAS. The current implementation lives under `quantas/Common/Concrete/Backends/BoostMq/` and uses Boost message queues for local IPC.

BoostMQ is a concrete local IPC backend. It is used to check whether QUANTAS algorithms can run as separate OS processes while keeping the normal `Peer` / `NetworkInterface` API.

Use it when you want evidence that an algorithm can run outside the single-process Abstract simulator. Do not treat it as full Abstract simulator parity yet.

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
  "maxMessageSizeBytes": 4096
}
```

Both settings are optional. Their defaults are `1000` messages and `4096` bytes. The control queue capacity is derived from `topology.initialPeers`; it is not a separate JSON setting. Before launching workers, `make mq` checks that the queues can be created and that every topology assignment fits within `maxMessageSizeBytes`.

A run writes one leader report named like `AlgorithmName_EXP<N>_leader_report.json`, plus one peer metrics file per peer. The leader report records `peerCount`, `testCount`, `rounds`, and each test's `completedPeerCount`.

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

The stricter `sent_total == received_raw_total` check can fail when fixed-round shutdown leaves final-round messages still queued. That is reported as pending-at-shutdown behavior, not automatically as modelled packet loss.

## Common failure messages

- queue creation failed → stale queues or insufficient shared-memory resources
- peer timeout → peer crashed or did not send `done`
- message too large → increase `boostMq.maxMessageSizeBytes` or reduce the payload size

## Currently, BoostMQ can truthfully promise only a limited unchanged-code contract.

| Behaviour | Current status |
|---|---|
| Compile the same  algorithm source | Supported |
| Create one local algorithm peer per OS process | Supported |
| Preserve peer IDs and configured peer count | Supported |
| Send each peer its ID and neighbour topology | Supported |
| Run local computation and message send/receive | Supported |
| Pass JSON parameters to each local peer | Supported, but the hook sees only that peer |
| Execute configured rounds and tests | Supported mechanically, not globally lockstep |
| Produce per-peer metric files | Supported |
| Produce leader completion and transport reports | Supported |

What is only partially supported:

| Behaviour | Current limitation |
|---|---|
| Complete membership | Leader knows everyone; each algorithm process receives only its own `Peer*` |
| Topology-dependent initialization | Neighbours are correct, but algorithms cannot inspect every live peer |
| Repeated tests | Implemented, but failure isolation is not yet proven |
| Experiment success | Process completion is reported, but semantic correctness is not guaranteed |
| Metrics | Transport counters are aggregated; algorithm-specific global results are not |
| Randomness | Each process has independent state; cross-process equivalence is undefined |

What BoostMQ currently cannot promise:

- Correct global role assignment for Byzantine, crashed, or miner roles.
- Exactly one experiment-wide action.
- A complete `vector<Peer*>` containing remote peers.
- Direct access to another peer’s fields or methods.
- Dynamic replacement of a remote peer object.
- Correct experiment-wide algorithm metrics.
- Abstract delay, drop, duplicate, reorder, or lockstep behaviour.
- That every existing algorithm behaves equivalently merely because it compiles and exits successfully.

Concrete example: PBFT currently receives a one-peer vector in every process. Each process can build a one-member committee and mark its own first peer Byzantine. That is not equivalent to Abstract mode selecting one Byzantine peer from the complete committee.
