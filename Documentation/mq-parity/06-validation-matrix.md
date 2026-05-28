# Validation Matrix

| Area | Scenario | Command(s) | Expected | Result | Evidence file/log |
|---|---|---|---|---|---|
| Lifecycle backbone | 11-peer Bitcoin smoke | `make -j4 mq_peer_debug mq_leader_debug` + `make mq_run_all INPUTFILE=quantas/BitcoinPeer/BitcoinPeerInput.json MQ_TOTAL_PEERS=11 MQ_ROUNDS=5` | all peers + leader exit `0` | PASS | terminal logs + `bitcoinRun_EXP1_p*.txt` |
| Stop handshake | done->stop flow | same as above | done notifications, leader stop broadcast, peer stop receive | PASS | coordinator logs |
| Final metrics | J13 baseline | same as above | `RunTime` and `Peak Memory KB` emitted | PASS | `bitcoinRun_EXP1_p*.txt` |
| Output isolation | per-peer artifacts | same as above | no cross-peer file corruption | PASS | peer-disambiguated files |
| Topology parity | TODO | TODO | topology-faithful behavior | TODO | TODO |
| Distribution parity | TODO | TODO | README semantics matched | TODO | TODO |
| `tests > 1` parity | TODO | TODO | repeated tests semantics matched | TODO | TODO |
