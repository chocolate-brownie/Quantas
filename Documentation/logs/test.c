numberOfPeers = 5;

initial state:

peer[0] knownNeighbors[]
peer[1] knownNeighbors[]
peer[2] knownNeighbors[]
peer[3] knownNeighbors[]
peer[4] knownNeighbors[]

1st iteration----------------------------------

for (int i = 0; i < 5; i++) {
    for (int j = i + 1; 1 < 5; j++) {
        if (0 != 1) {
            _peers[0]->addNeighbor(_peers[1]->internalId());
            _peers[1]>addNeighbor(_peers[0]->internalId());
        }
    }
}

peer[0] knownNeighbors[1]
peer[1] knownNeighbors[0]
peer[2] knownNeighbors[]
peer[3] knownNeighbors[]
peer[4] knownNeighbors[]

2nd iteration----------------------------------

for (int i = 0; i < 5; i++) {
    for (int j = 0 + 2; 2 < 5; j++) {
        if (0 != 2) {
            _peers[0]->addNeighbor(_peers[2]->internalId());
            _peers[2]>addNeighbor(_peers[0]->internalId());
        }
    }
}

peer[0] knownNeighbors[1, 2]
peer[1] knownNeighbors[0]
peer[2] knownNeighbors[0]
peer[3] knownNeighbors[]
peer[4] knownNeighbors[]

3rd iteration----------------------------------

for (int i = 0; i < 5; i++) {
    for (int j = 0 + 3; 3 < 5; j++) {
        if (0 != 3) {
            _peers[0]->addNeighbor(_peers[3]->internalId());
            _peers[3]>addNeighbor(_peers[0]->internalId());
        }
    }
}

peer[0] knownNeighbors[1, 2, 3]
peer[1] knownNeighbors[0]
peer[2] knownNeighbors[0]
peer[3] knownNeighbors[0]
peer[4] knownNeighbors[]

4th iteration----------------------------------

for (int i = 0; i < 5; i++) {
    for (int j = 0 + 4; 4 < 5; j++) {
        if (0 != 4) {
            _peers[0]->addNeighbor(_peers[4]->internalId());
            _peers[4]>addNeighbor(_peers[0]->internalId());
        }
    }
}

peer[0] knownNeighbors[1, 2, 3, 4]
peer[1] knownNeighbors[0]
peer[2] knownNeighbors[0]
peer[3] knownNeighbors[0]
peer[4] knownNeighbors[]

5th iteration----------------------------------

for (int i = 0; i < 5; i++) {
    for (int j = 0 + 5; 5 < 5; j++) {
        if (0 != 4) {
            _peers[0]->addNeighbor(_peers[4]->internalId());
            _peers[4]>addNeighbor(_peers[0]->internalId());
        }
    }
}

for (int i = 1; 1 < 5; i++) {
    for (int j = 1 + 1; 2 < 5; j++) {
        if (0 != 2) {
            _peers[1]->addNeighbor(_peers[2]->internalId());
            _peers[2]>addNeighbor(_peers[1]->internalId());
        }
    }
}

peer[0] knownNeighbors[1, 2, 3, 4]
peer[1] knownNeighbors[0, 2]
peer[2] knownNeighbors[0, 1]
peer[3] knownNeighbors[0]
peer[4] knownNeighbors[]

