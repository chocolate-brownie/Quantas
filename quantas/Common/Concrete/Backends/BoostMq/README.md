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

Run the full BoostMQ workflow from the repository root with:

```sh
make mq_run_all INPUTFILE=quantas/ExamplePeer/ExampleInput.json
```

This builds and runs the two BoostMQ binaries: `quantas_mq_leader.exe` and `quantas_mq_peer.exe`. The leader coordinates the experiment lifecycle and writes the leader report. The peer binary is launched once per peer and executes that peer's assigned rounds.

Use `MQ_TOTAL_PEERS=N` only when you need to override the peer count used by the makefile launcher. The leader still reads the experiment configuration from the JSON, so this value should match `topology.initialPeers` unless you are deliberately debugging launcher behavior. Use `MQ_ROUNDS=N` when you want to override the number of rounds from the input JSON for a quick run.

BoostMQ depends on the Linux POSIX message queue limit `/proc/sys/fs/mqueue/msg_max`. Linux commonly defaults this value to `10`, while the kernel hard cap is `65536`. Before launching peers, `mq_run_all` asks the leader to run a capacity preflight check. If the experiment needs more capacity than the system allows, the leader prints the required capacity and asks whether it should update the system limit.

Check the current system limit with:

```sh
cat /proc/sys/fs/mqueue/msg_max
```

If you accept the prompt, the leader runs:

```sh
sudo sysctl -w fs.mqueue.msg_max=N
```

This change applies to the current boot. If you decline the prompt or the `sysctl` command fails, the run stops before any peer binaries are launched.

A run writes one leader report named like `AlgorithmName_EXP<N>_leader_report.json`, plus one peer metrics file per peer.

## Common failure messages

- capacity check failed → raise `fs.mqueue.msg_max`
- queue creation failed → stale queues or low system limit
- peer timeout → peer crashed or did not send `done`
- message too large → increase `MAX_MSG_SIZE` or reduce payload size
