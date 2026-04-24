# Rendezvous Protocol — TCP vs Boost MQ

## TCP → MQ Function Reference

```mermaid
sequenceDiagram
    participant L as Leader
    participant F as Follower(s)

    Note over L: Step 1 — Create rendezvous point
    Note over L: TCP: ensureListenerStarted() :457<br/>socket() → bind() → listen()
    Note over L: MQ: create_only("quantas_barrier")

    Note over F: Step 2 — Create own inbox
    Note over F: TCP: ensureListenerStarted() :457<br/>each process binds own port
    Note over F: MQ: create_only("quantas_peer_N")

    F->>L: Step 3 — Send ready signal
    Note over F,L: TCP: sendRegistrationToLeader() :1244<br/>sendJson(leaderIp, leaderPort, {"type":"register",...})
    Note over F,L: MQ: barrier.send("ready")

    Note over L: Step 4 — Count N ready signals
    Note over L: TCP: handleRegister() :574<br/>stores in _processes map, checks total
    Note over L: MQ: barrier.receive() × N loop

    rect rgb(220,220,220)
        Note over L,F: SKIPPED IN MQ — TCP assignment phase only
        L-->>F: TCP: distributeAssignmentsIfReady() :651
        L-->>F: TCP: sendAssignmentToProcess() :730
        F-->>L: TCP: handleAssignment() :772
        F-->>L: TCP: markReady() :839
    end

    L->>F: Step 5 — Broadcast start
    Note over L,F: TCP: broadcastStartIfReady() :863<br/>checks allReady, sends "start" to each process
    Note over L,F: MQ: inbox.send("start") into each quantas_peer_N

    Note over F: Step 6 — Unblock and begin
    Note over F: TCP: handleStart() :922 sets flag<br/>waitForStartSignal() :931 blocks on condition_variable
    Note over F: MQ: inbox.receive("start") → begin simulation
```

## MQ Rendezvous Flow

```mermaid
sequenceDiagram
    participant L as Leader Process
    participant B as quantas_barrier
    participant F as Follower Process(es)
    participant I as quantas_peer_N

    Note over L,B: Step 1 — Leader creates the rendezvous point
    L->>B: create_only("quantas_barrier")

    Note over F,I: Step 2 — Followers create their own inboxes
    F->>I: create_only("quantas_peer_N")

    Note over F,B: Step 3 — Followers signal ready
    F->>B: send("ready")

    Note over L,B: Step 4 — Leader counts N ready signals
    B-->>L: receive() × N

    rect rgb(220, 220, 220)
        Note over L,F: SKIPPED in MQ — TCP only (assignment phase)
        L-->>F: [TCP] send peer ID + neighbor topology assignments
        F-->>L: [TCP] send second ready signal after applying assignment
    end

    Note over L,I: Step 5 — Leader sends start to every peer inbox
    L->>I: send("start") into each quantas_peer_N

    Note over F,I: Step 6 — Followers unblock and begin simulation
    I-->>F: receive("start") → begin
```

## Why the assignment phase is skipped in MQ

In TCP, the leader must discover each process's IP and port, assign peer IDs, and broadcast topology — because nothing is known at startup.

In MQ, queue names are derived directly from peer IDs (`quantas_peer_<id>`). Every process already knows how to reach any other peer. No discovery or assignment needed.
