# ConcreteMQ v1

ConcreteMQ is an experimental multi-process backend for QUANTAS. It runs one OS process per peer and uses Boost message queues for local IPC.

Use it when you want evidence that an algorithm can run outside the single-process Abstract simulator. Do not treat it as full Abstract simulator parity yet.

## Build

```sh
make -j4 mq_peer_debug mq_leader_debug
```

For faster runs:

```sh
make -j4 mq_peer_release mq_leader_release
```

## Run

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
- Makefile orchestration is not a production process manager.
- MQ v1 does not implement full Abstract channel semantics such as configured delay, duplicate, reorder, and `maxMsgsRec` behavior.
- Large complete topologies may hit process, queue, or message-size limits.
- The leader report embeds peer metrics, but final Abstract-style metric aggregation is still a separate step.

## Future ZeroMQ Backend

ConcreteMQ v1 proves the protocol shape: ready, assignment, start, peer execution, metrics flush, done, stop, and leader report.

The future ZeroMQ backend is expected to address the larger-scale transport and orchestration limitations instead of over-investing in Boost message queue scaling.
