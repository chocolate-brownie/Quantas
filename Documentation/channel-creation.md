# Channel Creation — Visual Guide

## Diagram 1: 3-peer complete network after createInitialChannels()

Each arrow is one Channel object (one directional string).
6 peers × (n-1) neighbors = 3 peers × 2 = 6 channels total.

```mermaid
flowchart LR
    P0((Peer 0))
    P1((Peer 1))
    P2((Peer 2))

    P0 -->|"channel: peer0 → peer1"| P1
    P1 -->|"channel: peer1 → peer0"| P0

    P0 -->|"channel: peer0 → peer2"| P
    P2 -->|"channel: peer2 → peer0"| P0

    P1 -->|"channel: peer1 → peer2"| P2
    P2 -->|"channel: peer2 → peer1"| P1
```

---

## Diagram 2: What happens when one channel is created (peer0 → peer1)

The same `channelPtr` (one object) gets registered on **both** peers.

```mermaid
sequenceDiagram
    participant Network
    participant Channel as channelPtr (peer0→peer1)
    participant NI_peer1 as peer1's NetworkInterface
    participant NI_peer0 as peer0's NetworkInterface

    Network->>Channel: make_shared<Channel>(target=peer1, source=peer0)
    Note over Channel: _packetQueue = []<br/>source = peer0<br/>target = peer1

    Network->>NI_peer1: addInboundChannel(peer0's ID, channelPtr)
    Note over NI_peer1: _inBoundChannels[peer0] = channelPtr<br/>("I listen for messages from peer0 here")

    Network->>NI_peer0: addOutboundChannel(peer1's ID, channelPtr)
    Note over NI_peer0: _outBoundChannels[peer1] = channelPtr<br/>("I send messages to peer1 through here")
```

---

## Diagram 3: Full picture — peer's NetworkInterface after setup (peer 0, 3-peer network)

```mermaid
flowchart TD
    subgraph Peer0["Peer 0"]
        subgraph NI0["NetworkInterface"]
            OUT["_outBoundChannels
            peer1 → channel_0to1
            peer2 → channel_0to2"]
            IN["_inBoundChannels
            peer1 → channel_1to0
            peer2 → channel_2to0"]
            STREAM["_inStream (inbox)
            populated by receive()"]
        end
        ALGO["performComputation()
        reads from _inStream
        writes via unicastTo()"]
    end

    OUT -->|"pushPacket()"| C1["channel_0to1 → peer1"]
    OUT -->|"pushPacket()"| C2["channel_0to2 → peer2"]
    C3["channel_1to0 ← peer1"] -->|"popPacket()"| IN
    C4["channel_2to0 ← peer2"] -->|"popPacket()"| IN
    IN -->|"receive() delivers arrived packets"| STREAM
    STREAM -->|"popInStream()"| ALGO
```
