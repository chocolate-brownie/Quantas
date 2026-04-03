In this file I document what I do everyday during my internship.

### 01/04/2026
- I studied a little bit about distributed systems and algorithms and how they works. How an algorithm of these systems always has to be data consistent, fault tolerance, concurrency controlled and resource managed.
- Setup GITLAB and Developer environment
- Reading the paper about Quantas

### 02/04/2026
    - Continuing to read the Quantas paper. Following the sim.run() function at Simulation.hpp file, learning about different components in the JSON files and how they connects with the testing of algorithm.
- How performance can be enhanced using threads for each peer to run performComputation()
    - The JSON config doesn't just connect to "testing" — it defines the entire experiment: topology, network conditions, algorithm parameters, and how many repetitions to run. Testing is one use, but so is data collection for research

### 03/04/2026
- Continuing to read the Quantas paper, following the code
- Today I understood most of the stuff in the simulation componenet, until we hit the for loop that runs number of tests times mentioned in the JSON file.. the `Simulation.run()` kinda preps the by extracting data from the JSON, setting up threads, managing the output logfile. Now I am reading the `Network.hpp` and `Network.cpp` which comes in to play inside the for loop. It does three things in order

    1. Create peers (lines 29-32) — uses PeerRegistry::makePeer() to
    instantiate the right peer type from the JSON string
    2. Set up neighbors (lines 42-64) — picks the topology function
    (fullyConnect, chain, ring, etc.) and calls addNeighbor() on each peer
    3. Create channels (line 66) — createInitialChannels() wires up actual
    Channel objects between every pair of neighbors

