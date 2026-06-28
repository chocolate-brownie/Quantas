# Communication backend using C++ Boost Message Queues

[Boost.Interprocess Documentation](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html)

Using boost message queues I have implemented an experimental multi-process commuincation backend for QUANTAS. The current implementation lives under `quantas/Common/Concrete/Backends/BoostMq/` and uses Boost message queues for local IPC.

Use it when you want evidence that an algorithm can run outside the single-process Abstract simulator. Do not treat it as full Abstract simulator parity yet.

## Source Layout

```text
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

`ConcreteMQ` is still used in target names and executable names for continuity. `BoostMq` is the folder name that identifies the transport implementation.

## Build

```sh
make -j4 mq_peer_debug mq_leader_debug
```

For faster runs:

```sh
make -j4 mq_peer_release mq_leader_release
```

## Run

BoostMQ backend is operated with two binaries:

- `mq_leader`: parses the input file, owns experiment-level setup, creates the shared control queues, assigns topology, broadcasts the start signal, waits for completion, and writes the leader report.
- `mq_peer`: runs one peer process. The peer receives its assignment from the leader, executes the configured rounds, writes peer metrics, and sends completion status back to the leader.

The main command is:

```sh
make mq_run_all INPUTFILE=quantas/BitcoinPeer/BitcoinPeerInput.json
```

`mq_run_all` reads the first `topology.initialPeers` value from the input JSON and launches that many peer processes automatically.

You can still override the peer count when needed:

```sh
make mq_run_all INPUTFILE=quantas/PBFTPeer/PBFTInput.json MQ_TOTAL_PEERS=20
```

For shorter validation runs, override the round count:

```sh
make mq_run_all INPUTFILE=quantas/RaftPeer/RaftInput.json MQ_ROUNDS=50
```

### BoostMQ Control-Plane Capacity

BoostMQ uses two shared control queues before and after the algorithm run:

```text
mq_barrier  ready signals from peers to leader
mq_done     done signals from peers to leader
```

For deterministic startup and shutdown, the control queues must be able to hold the peer signaling burst for the experiment:

```text
required_barrier_capacity = peer_count
required_done_capacity    = peer_count
```

The leader is responsible for this preflight check because it already parses the experiment JSON before creating `mq_barrier` and `mq_done`.

The safe condition is:

```text
required_capacity <= configured_queue_capacity <= fs.mqueue.msg_max
```

For example, a 200-peer experiment requires control queues with capacity at least 200:

```text
peer_count = 200
required control capacity = 200
```

On Linux, Boost message queues are also capped by the kernel setting:

```sh
cat /proc/sys/fs/mqueue/msg_max
```

Many development machines default to `10`. If the experiment requires 200 control messages but the system allows only 10, the leader must not create the queues or start the protocol yet.

### Interactive Capacity Tuning

The intended large-run operator flow is:

```text
1. Leader parses the input JSON.
2. Leader computes required control capacity from peer_count.
3. Leader reads /proc/sys/fs/mqueue/msg_max.
4. If the system limit is sufficient, leader creates mq_barrier and mq_done with the required capacity.
5. If the system limit is too small, leader prints the required capacity and current system limit.
6. In interactive tuning mode, leader asks whether to run sudo sysctl.
7. If accepted, leader runs:
   sudo sysctl -w fs.mqueue.msg_max=<required_capacity>
8. Leader re-reads /proc/sys/fs/mqueue/msg_max.
9. If the new limit is sufficient, leader creates the control queues and starts the run.
10. If the new limit is still insufficient, leader exits before protocol start.
```

The leader must perform this before any peer depends on `mq_barrier` or `mq_done`. The program should never read or store a sudo password itself; `sudo` owns password prompting.

Non-interactive runs should fail fast with remediation instructions instead of waiting for terminal input. Interactive tuning should be enabled explicitly, for example by a future leader flag or make variable such as:

```sh
make mq_run_all INPUTFILE=quantas/PBFTPeer/PBFTInput.json MQ_TOTAL_PEERS=200 MQ_TUNE_CAPACITY=1
```

If tuning is not enabled, the user can apply the same change manually before running:

```sh
sudo sysctl -w fs.mqueue.msg_max=200
make mq_run_all INPUTFILE=quantas/PBFTPeer/PBFTInput.json MQ_TOTAL_PEERS=200
```

For persistent machine setup, place the setting in a sysctl configuration file such as `/etc/sysctl.d/99-quantas-mqueue.conf`:

```text
fs.mqueue.msg_max = 200
```

## Outputs

ConcreteMQ writes one leader report and one metric file per peer/test.

Leader report:

```text
<logBase>_EXP<N>_leader_report.json
```

Peer metric files:

```text
<logBase>_EXP<N>_p<peerId>_TEST<N>.txt
```

The leader report records:

- backend name (`mq` for this implementation)
- expected peers
- completed peers
- missing peers
- timeout status
- per-peer output paths
- embedded per-peer `peerMetrics`

## Validated Scope

ConcreteMQ v1 has been validated for dependable runs up to 20 peers.

Current useful validation set:

```sh
make mq_run_all INPUTFILE=quantas/AltBitPeer/AltBitUtility.json
make mq_run_all INPUTFILE=quantas/BitcoinPeer/BitcoinPeerInput.json MQ_ROUNDS=25
make mq_run_all INPUTFILE=quantas/PBFTPeer/PBFTInput.json MQ_ROUNDS=20
make mq_run_all INPUTFILE=quantas/RaftPeer/RaftInput.json MQ_ROUNDS=50
```

This covers repeated tests, 11-peer Bitcoin, and 20-peer consensus-style workloads.

## Limitations

ConcreteMQ v1 is not a full replacement for Abstract QUANTAS.

Current limitations:

- Validated dependable threshold is 20 peers.
- 32+ peers should be treated as stress tests.
- 64/100/300-peer runs are not guaranteed in v1.
- Large peer counts require control-plane capacity validation and may require raising `fs.mqueue.msg_max` before the leader creates `mq_barrier` and `mq_done`.
- Makefile orchestration is not a production process manager.
- MQ v1 does not implement full Abstract channel semantics such as configured delay, duplicate, reorder, and `maxMsgsRec` behavior.
- Large complete topologies may hit process, queue, or message-size limits.
- The leader report embeds peer metrics, but final Abstract-style metric aggregation is still a separate step.
- IPC counters are still minimal. Backpressure drops are logged as `dropped_backpressure`, but full per-peer `sent`, `received_raw`, and `delivered_to_instream` reporting is still a TODO.

## Future ZeroMQ Backend

ConcreteMQ v1 proves the protocol shape: ready, assignment, start, peer execution, metrics flush, done, stop, and leader report.

The future ZeroMQ backend is expected to address the larger-scale transport and orchestration limitations instead of over-investing in Boost message queue scaling.
