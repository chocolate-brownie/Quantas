## **QUANTAS: Quantitative User-friendly Adaptable Networked Things Abstract Simulator** 

JOSEPH OGLIO, KENDRIC HOOD, MIKHAIL NESTERENKO, Kent State University, Kent, USA 

SÉBASTIEN TIXEUIL, Sorbonne Université, CNRS, LIP6, FR-75005, France 

We present QUANTAS: a simulator that enables quantitative performance analysis of distributed algorithms. It has a number of attractive features. QUANTAS is an abstract simulator, therefore, the obtained results are not affected by the specifics of a particular network or operating system architecture. QUANTAS allows distributed algorithms researchers to quickly investigate a potential solution and collect data about its performance. QUANTAS programming is relatively straightforward and is accessible to theoretical researchers. To demonstrate QUANTAS capabilities, we implement and compare the behavior of two representative examples from four major classes of distributed algorithms: blockchains, distributed hash tables, consensus, and reliable data link message transmission. 

## **1 INTRODUCTION** 

Theoretical work in distributed algorithms often involves establishing a possibility of solution existence, proving an algorithm correct or determining its message or computation complexity. If the new algorithm improves on the existing ones, this improvement is quantified in terms of these complexity metrics. These approaches may be lacking as they do not provide sufficient insight into the realistic behavior of the algorithm. Indeed, hidden constants and system parameters, such as message delay or relative computation power, influence the performance of most algorithms. 

Alternatively, the algorithm is implemented in a real distributed architecture such as a computer cluster, a collection of virtual machines, a cloud computing service [13], or using a general purpose network simulator such as ns-3 [25] or OMNET++ [28]. Although such efforts demonstrate practical algorithm implementation and enable its immediate application, the obtained results make it difficult to separate the operation of the algorithm from the influence of the particular network and operating system used in performance evaluation. For example, it is unclear how the interaction between virtual machines and network switches at the server farm affects the results obtained in real network performance evaluation or whether the selection of a particular physical layer network protocol made a difference in a network simulator. 

Another obstacle for these approaches is difficulty of performing large-scale performance evaluation. Large scale physical systems are expensive to procure for experimentation. Moreover, in a physical system, there is difficulty instrumenting and then ascertaining conditions of interest for experimenter such as specific network delay. Network simulators, due to extensive simulation detail, also have limited scalability. 

To demonstrate the behavior of a distributed algorithm and compare it with the alternatives, abstract simulation may be used. Abstract simulation closely follows the communication and computation model used in distributed algorithms research. The algorithm is represented as a collection of processes and communication channels or shared variables. The computation is modeled as series of simulation rounds where processes perform concurrent processing and exchange messages. Such modelling of algorithms make abstract simulation attractive to distributed algorithms researchers. 

However, we believe there is a lack of general purpose tools for such abstract simulation. Most papers use ad hoc one-off implementations built for one paper [3, 7, 12, 16] or, at best, a domain specific abstract simulation that is reused for a limited number of papers [4, 5, 22]. This duplicates effort and makes it difficult to verify obtained results, compare 

> Authors’ addresses: Joseph Oglio, Kendric Hood, Mikhail Nesterenko, joglio@kent.edu,khood5@kent.edu,mikhail@cs.kent.edu, Kent State University, Kent, USA; Sébastien Tixeuil, Sebastien.Tixeuil@lip6.fr, Sorbonne Université, CNRS, LIP6, FR-75005, France. 

1 

Joseph Oglio, Kendric Hood, Mikhail Nesterenko and Sébastien Tixeuil 

2 

them across several publications, or make further improvements. The simulation code and obtained data are seldom made publicly available which further exacerbates the problem. 

The existing general purpose abstract simulators, that we are aware of, tend to be used for education rather than research. However, we think that the focus of an educational simulator differs from that of a research simulator. Indeed, the major concern of an educational simulator is to give novices an exposure to the distributed algorithm operation, and a visual representation of the algorithm as it executes [2, 10, 19]. Therefore, simplicity and ease of use are of primary importance. While simplicity certainly does not harm a distributed algorithm simulator, other important characteristics such as scalability, simulation speed, versatility, and ability to obtain quantitative measures for metrics of interest come to the fore. The closest simulation framework we could find is Neko [27], however the project seems no longer maintained and its source code no longer available. 

In this paper, we present QUANTAS: an abstract simulator specifically designed for distributed algorithms research. Our primary research area is distributed algorithms. The development of QUANTAS arose out of our own need to do performance evaluation. We, therefore, built QUANTAS to satisfy the needs of researchers similar to our own. We used QUANTAS prototype in several studies [16, 24]. The QUANTAS software is publicly available [1] for other researchers to download and use. 

## **2 SIMULATOR DESIGN PRINCIPLES, ARCHITECTURE AND SETUP** 

## **Design principles.** 

- The foremost principle is simplicity, ease of use, and ability to obtain quantitative results quickly. That is, a newcomer should be able to implement algorithms, and get simulation data in a relatively straightforward manner. Quantas interfaces remain basic for ease of integration with analysis or input generation tools. The simulator core is coded in C++. No further compilers, libraries, or specialized languages for distributed algorithm specification are used. In our experience, the benefits of such constructs are limited: what is gained it simplicity is lost in flexibility and speed. Parameter customization and experiment set up is done through simple text-based configuration files. 

- As a starting point and demonstration of the simulator capabilities, we provide a set of representative examples. We think these examples will be used with minor modifications by a majority of QUANTAS users for their own research experimentation. 

- Once the basic behavior of a distributed algorithm is ascertained, the researchers usually want to observe its behavior at scale: large system size, extensive simulated time or resource usage. To support this, we implemented QUANTAS in C++ with minimum overhead. C++ threading is used to implement concurrent simulation of multiple procsses. Potentially, the simulated network size is limited by the host computer processor and memory resources. 

- The major simulation goal is to obtain data for analysis and presentation. QUANTAS is configured to output simulation data in JSON format for ease of further processing. One can then use various available interactive or automated tools to analyze and plot the data. 

- QUANTAS combines all these features in a relatively compact, modular codebase which is easily extensible and modifiable. QUANTAS contains approximately 4 _,_ 000 lines of C++ code. 

**Terms and operation.** A simulated _distributed algorithm_ operates on a list of nodes connected via unicast channels. Each channel connects a single sender and a single receiver. Every node has a unique identifier. Each _computation_ of 

QUANTAS: Quantitative User-friendly Adaptable Networked Things Abstract Simulator 

3 

**==> picture [254 x 249] intentionally omitted <==**

**----- Start of picture text -----**<br>
Simulation<br>configures simulation, sets up logs, initializes<br>network topology and state, runs simulation<br>Network<br>determines network topology: connects  «user-provided»<br>nodes and channels, sets up channel delays<br>Configuration<br>processing config files: topology, delay, size,<br>simulation length, etc.<br>Abstract Node<br>node computation (command) interface<br>Concrete Node<br>algorithms-specific commands and variables,<br>libraries<br>Node Network Interface<br>channel head/tail, message sending and<br>delivery, implementing message delay,<br>handles sending and receiving of packets<br>(messages) between nodes.<br>Packet<br>packet header: source, destination, packet  User-Defined Message<br>delay message payload<br>**----- End of picture text -----**<br>


Fig. 1. QUANTAS architecture. 

the distributed algorithm is a sequence of receive-compute-send _rounds_ . Each round has three _phases_ : receive messages, perform local computation, and send newly formed messages. A computation _length_ is its number of rounds. A message takes at least one round to pass between nodes that are directly connected through a channel. A message can be delayed. Delay length is configured. The delay is also configured to be either deterministic, uniformly random, or following a Poisson distribution. Communication channels are FIFO by default. Other message propagation delay disciplines may be added by the user. A transmitted message may be configured to be lost with a certain probability. A message may be sent to an individual process or broadcast to the entire network. A single _run_ of the QUANTAS simulator executes several algorithm computations with the same parameters. This allows QUANTAS to execute multiple individual experiments for a single data point. 

**Architecture.** QUANTAS architecture is shown in Figure 1. The components represent the larger C++ templates and classes. The components are in two categories: user-provided and the simulator proper. The user-provided components encode the algorithm to be simulated. The simulator proper components carry out the simulation. The run-time operation of the simulator is controlled by configuration files. 

The _Simulation Component_ configures and initializes the simulation run. It then carries out the receive-computesend computation rounds of individual computations of the run. The Simulation Component uses the _Configuration Component_ for processing user-supplied configuration file containing network topology and size, parameters of the run, message delay discipline and parameters, computation length, etc. The network topology is specified as adjacency list 

Joseph Oglio, Kendric Hood, Mikhail Nesterenko and Sébastien Tixeuil 

4 

**struct** HelloMessage { std:: string messageText; }; ... **void** performComputation () { HelloMessage msg; msg.messageText = "Hello From " + std:: to_string(id()); // send "hello" to all processes broadcast(msg); // service all received messages **while** (! inStreamEmpty ()) { Packet <HelloMessage > newMsg = popInStream (); // logger is a Singleton , "Greetings" is a tag // getRound () returns simulated computation round LogWriter :: instance()-> data["Greetings"]. push_back(newMsg.getMessage (). messageText + "at round: " + getRound ()) } } 

Fig. 2. Example QUANTAS code for a local computation phase: each process broadcasts a single message and receives messages from its neighbors. 

and can be generated by hand or by a separate tool. This component maintains a thread pool to concurrently execute rounds of multiple simulated processes. 

The _Network Component_ configures distributed algorithm topology, sets up communication channels and executes receive- compute- and send- phases of the round. The _Abstract Node Component_ is a C++ abstract class that lists the interfaces to be implemented by a user-provided _Concrete Node_ component. The main part of this interface is the code to be executed in local computation phase of the round. 

The _Node Network Interface Component_ executes receive and send phases of the round. In the receive phase, The _Node Network Interface Component_ examines all the channels, and determines if any of the messages currently in transit are ready to be received. The ready-to-receive messages are made available for the computation phase. If the computation phase generates messages to be transmitted, the _Node Network Interface Component_ collects them and puts them in the appropriate destination channels. 

A message is enclosed in a packet. The packet contains the source, destination, and the delay for this particular message. The _Packet Component_ provides this header and the _Node Network Interface Component_ uses this header for message routing. The actual message format and its payload are provided by the _User-Defined Message Component_ . 

Let us discuss QUANTAS data output capabilities. QUANTAS provides a global logging facility, so that each component may output to the log file. All simulator components may output data about their particular operation. For example, the _Node Network Interface Component_ may output sender and receiver identifiers for each individual message. The user-provided components may output arbitrary data, which enables user-specific metrics to be easily implemented. User-provided components have access to the computation round number maintained by the simulator. This round number can be included in the output for analysis. To simplify later processing, logger allows attach an arbitrary tag to output lines. QUANTAS example code is shown in Figure 2. 

QUANTAS: Quantitative User-friendly Adaptable Networked Things Abstract Simulator 

5 

Fig. 3. Blockchain throughput during a computation. 

## **3 EXAMPLES** 

In this section, we demonstrate how QUANTAS may be adapted to fit diverse experimentation needs of distributed algorithms researchers. We chose four domains and a pair of previously published well-known algorithms in each. We then implemented the algorithms in QUANTAS and compared their performance. While the results themselves are not surprising, they demonstrate how QUANTAS may be used for performance evaluation of a variety of algorithms. 

**Blockchains.** Blockchain is a secure distributed ledger maintained by a network of peers that compete to add blocks of transactions to the tail of the chain. We simulate simplified versions of the two most widely used blockchain algorithms: Bitcoin [23] and Ethereum [29]. The peer-to-peer system has 20 peers. Each peer maintains its own copy of the blockchain. A transaction is submitted to a random peer with probability 5% per peer per round, i.e. on average, it is one transaction per round. The peer receiving the transaction broadcasts it to the rest of the network. In Bitcoin, each peer mines one of the received transactions attempting to link it to the longest chain. The mining probability for each peer is 2 _._ 5%. In Ethereum, each block links to all previously unlinked blocks. The single computation was executed for 100 rounds. Each simulator run had 10 experiments. 

The results are shown in Figure 3. We estimate the number of confirmed blocks by considering the longest chain for each peer and determining the shortest among those. For each blockchain algorithm, we executed a computation for 100 rounds and calculated the average number of blocks per round. Figure 3 shows a moving average of this value with a window of 5 rounds. The results shown for message delays 1 through 10. The results indicate that our implementation of Ethereum has better throughput than Bitcoin since Ethereum, unlike Bitcoin, may confirm multiple competing blocks concurrently. 

**Robust Consensus.** In robust consensus, a network of nodes attempts to agree on a single input value. We simulated two resilient consensus algorithms: PBFT [11] and Raft [17]. Both algorithms process a sequence of consensus requests. PBFT is resilient to Byzantine faults [20]. In our implementation of PBFT, there is a fixed leader process _𝑙_ . The leader _𝑙_ has a sequence of values to commit. For the confirmation, _𝑙_ consults the rest of the processes. For each individual value, _𝑙_ executes PBFT. Specifically, _𝑙_ broadcasts _pre-prepare_ message to all processes with the value to be committed. 

6 

Joseph Oglio, Kendric Hood, Mikhail Nesterenko and Sébastien Tixeuil 

Fig. 4. Consensus. Latency of achieving a single decision vs message delay. 

Fig. 5. Reliable Data Link. Message utility depending on message loss. 

Once a processes receive sufficiently many _prepare_ messages with the same value, it commits the value and sends _commit_ message informing everyone of this. After receiving sufficiently many _commit_ s, _𝑙_ considers this PBFT instance terminated and moves on to committing the next value. The leader change is not implemented. 

Raft [17] is resilient to crashes and churn but not to Byzantine faults. In Raft, the leader _𝑙_ broadcasts requests to all nodes in the system and waits for majority of responses. After receiving this majority, _𝑙_ moves to the next value to be committed. 

The commitment _latency_ is the number of rounds it takes the algorithm from the moment the initial message is transmitted by the leader until the last required commit message is received by the leader. We used 20 processes, we executed a computation for 1 _,_ 000 rounds. Each simulator run had 10 computations. We average commitment latency across the run. We varied message delay and recorded the latency of RAFT and PBFT. The results are shown in Figure 4. RAFT has significantly lower commitment latency than PBFT as there are fewer rounds of message exchanges. This speed is obtained at the expense of resiliency to Byzantine faults. 

**Reliable Data Link.** In a data link algorithm, the sender process attempts to transmit data to the receiver process despite message loss in the communication channel. A self-stabilizing algorithm [14] is resilient to global state corruption. We implemented two self-stabilizing data link algorithms: alternating-bit protocol (ABP) [18] and stabilizing-data link algorithm (SDL) [15]. ABP requires FIFO channels. SDL operates correctly even in non-FIFO channels. In ABP, the sender transmits a single data message and waits for acknowledgement from the receiver. If either the data message or the acknowledgement is lost, the sender times out and retransmits the message. In SDL, to enable the receiver to get messages in correct order in a non-FIFO channel, the sender transmits the same message multiple times. The number of transmissions is determined by maximum channel size. 

To compare the two algorithms, we used channels of size one. For SDL, this channel size means that the sender sends the same message 5 times. In our simulation, we used 2 processes: the sender and the receiver. We executed the computation for 100 rounds. Each simulator run was 10 computations. We computed _message utility_ — the ratio of successfully received message over transmitted. We varied message loss rate and recorded the utility of the two algorithms. 

7 

QUANTAS: Quantitative User-friendly Adaptable Networked Things Abstract Simulator 

Fig. 6. Distributed Hash Table query speed. 

Fig. 7. Distributed Hash Table simulation speedup. 

The results are shown in Figure 5. In our simulation, as message loss increased, both algorithms had to submit more messages to get the data across. This means that the utility decreased for both algorithms. However, SDL effectively submitted about five times as many messages as ABP. This is the overhead needed by the SDL to enforce sequential message delivery in a non-FIFO channel. 

**Distributed Hash Tables.** In a distributed hash table (DHT), a peer-to-peer system provides query service for key to data items spread throughout the network. The algorithm is optimized to minimize the number of lookups per query. Some of the most widely used DHTs are Chord [26] and Kademlia [21]. 

In our Chord implementation, the peer identifiers form a ring. Shortcut links are not implemented. A query for an identifier chosen uniformly at random is generated by another random node. The query is routed to the destination node in the shortest direction. 

In Kademlia, on top of this basic Chord implementation, we also build shortcut links as follows. Peer identifiers are treated as bit fields. A prefix peer group for a particular peer _𝑝_ is a set of peers whose identifiers share a prefix of particular length _𝑙_ with _𝑝_ and differ from _𝑝_ at length _𝑙_ + 1. For example, if the prefix is one bit less than the complete id length, then, there is a single member in this peer group. A peer group for a prefix that is two bits shorter than id length, contains two members. A peer group three bit shorter than id length contains 4 members and so on. For each group, a peer selects a random member and creates a shortcut link to it. The query routing is as follows. The peer selects a member with the closest prefix to the destination and routes the query there. 

The results are shown in Figure 6. The results indicate that our implementation of Kademlia outperforms Chord because Kademlia query routes are logarithmic with respect to the network size. 

To test QUANTAS parallel performance, we varied the number of threads in the simulator thread pool and measured the runtime of the simulation. We simulated 500 peers. Each computation was 100 rounds. We run 10 computations per data point. The simulator used approximately 40 GB of RAM and ran on a virtual machine with a host machine having 2 Intel Xeon Gold 6132 CPUs running at 2.60 GHz. The virtual machine had 12 cores. The results are shown in Figure 7. The results indicate that the simulation speed increases as more threads are added to the simulator thread pool. This speed increase ends as all available host processor cores are used for the concurrent simulation. 

Joseph Oglio, Kendric Hood, Mikhail Nesterenko and Sébastien Tixeuil 

8 

## **4 FUTURE WORK** 

We anticipate further enhancements of QUANTAS capabilities. Besides already implemented message loss, we would like to add more sophisticated fault injection. In particular, we plan to add support for self-stabilizing algorithms evaluation. Even though a self-stabilizing algorithm is proven to recover from an arbitrary global state, evaluating the algorithm’s performance starting from a state generated uniformly at random is not realistic as not all such states are equally likely to appear. A more sophisticated approach was developed by Adamek et al [6]: an achievable state of a self-stabilizing algorithm is selectively perturbed. We would like to implement this kind of fault-injection in QUANTAS. Adding crash faults would be a simple and useful addition. A more challenging task is adding Byzantine faults since Byzantine processes are expected to behave so as to inflict the most damage to the algorithm. Hence, despite extensive studies of Byzantine fault tolerance, few of them have performance evaluation. We believe adding Byzantine fault injection [8] would be helpful to the research community. 

We would like to add random topology generation that QUANTAS so that the processes and channels are configured randomly according to the topology graph parameters provided in the configuration file, for example a random graph with the specified node number and edge probability. Another feature we find useful is facilitation of application level separation. This would allow the simulator to evaluate levels of multi-level algorithms separately, for example, evaluate the same consensus algorithm over different broadcast algorithms. 

Another important issue is scalability increase, be it related to the number of simulated nodes [9] or the number of simulations per data point to obtain quantitative results. To improve the performance of QUANTAS at scale, we plan to pursue multithreading inside the simulator more aggressively. For example, by using parallel algorithms in the Standard Template Library of C++. In the future, we would like to explore distributed multi-computer simulation. 

In this paper, we presented QUANTAS, a general abstract simulator dedicated to distributed algorithms quantitative evaluation. While we provided a number of case studies, we welcome contributions from the Distributed Computing community, to build a library of ready-to-use templates for most algorithmic paradigms, that enables fair comparison with previous work when designing new solutions. We believe that QUANTAS fulfils the need for an abstract simulator among researchers of distributed algorithms and we hope it proves to be useful and convenient. 

## **ACKNOWLEDGMENTS** 

We are thankful to Shishir Rai of Kent State University for providing code for an example as the simulator was being developed. 

## **REFERENCES** 

- [1] Quantas: Quantitative user-friendly adaptable networked things abstract simulato. https://github.com/QuantasSupport/Quantas. 

- [2] Sinalgo: simulator for network algorithms. https://sinalgo.github.io/. 

- [3] Jordan Adamek, Giovanni Farina, Mikhail Nesterenko, and Sébastien Tixeuil. Evaluating and optimizing stabilizing dining philosophers. _J. Parallel Distributed Comput._ , 109:63–74, 2017. 

- [4] Jordan Adamek, Mikhail Nesterenko, James Scott Robinson, and Sébastien Tixeuil. Stateless reliable geocasting. In _36th IEEE Symposium on Reliable Distributed Systems, SRDS 2017, Hong Kong, Hong Kong, September 26-29, 2017_ , pages 44–53. IEEE Computer Society, 2017. 

- [5] Jordan Adamek, Mikhail Nesterenko, James Scott Robinson, and Sébastien Tixeuil. Concurrent geometric multicasting. In Paolo Bellavista and Vijay K. Garg, editors, _Proceedings of the 19th International Conference on Distributed Computing and Networking, ICDCN 2018, Varanasi, India, January 4-7, 2018_ , pages 9:1–9:10. ACM, 2018. 

- [6] Jordan Adamek, Mikhail Nesterenko, and Sébastien Tixeuil. Evaluating practical tolerance properties of stabilizing programs through simulation: The case of propagation of information with feedback. In Andréa W. Richa and Christian Scheideler, editors, _Stabilization, Safety, and Security_ 

QUANTAS: Quantitative User-friendly Adaptable Networked Things Abstract Simulator 

9 

_of Distributed Systems - 14th International Symposium, SSS 2012, Toronto, Canada, October 1-4, 2012. Proceedings_ , volume 7596 of _Lecture Notes in Computer Science_ , pages 126–132. Springer, 2012. 

- [7] Jordan Adamek, Mikhail Nesterenko, and Sébastien Tixeuil. Evaluating and optimizing stabilizing dining philosophers. In _2015 11th European Dependable Computing Conference (EDCC)_ , pages 233–244. IEEE, 2015. 

- [8] Ali Asim and Sébastien Tixeuil. Advanced faults patterns for WSN dependability benchmarking. In Violet R. Syrotiuk, Fatih Alagöz, Brahim Bensaou, and Özgür B. Akan, editors, _Proceedings of the 13th International Symposium on Modeling Analysis and Simulation of Wireless and Mobile Systems, MSWiM 2010, Bodrum, Turkey, October 17-21, 2010_ , pages 39–48. ACM, 2010. 

- [9] Ali Asim and Sébastien Tixeuil. Xs-wsnet: Extreme scale wireless sensor network simulation. In _11th IEEE International Symposium on a World of Wireless, Mobile and Multimedia Networks, WOWMOM 2010, Montreal, QC, Canada, 14-17 June, 2010_ , pages 1–9. IEEE Computer Society, 2010. 

- [10] Arnaud Casteigts. Jbotsim: a tool for fast prototyping of distributed algorithms in dynamic networks. In Georgios Theodoropoulos, editor, _Proceedings of the 8th International Conference on Simulation Tools and Techniques, Athens, Greece, August 24-26, 2015_ , pages 290–292. ICST/ACM, 2015. 

- [11] Miguel Castro, Barbara Liskov, et al. Practical byzantine fault tolerance. In _OsDI_ , volume 99, pages 173–186, 1999. 

- [12] Thomas Clouser, Mark Miyashita, and Mikhail Nesterenko. Fast geometric routing with concurrent face traversal. In _International Conference On Principles Of Distributed Systems_ , pages 346–362. Springer, 2008. 

- [13] Marshall Copeland, Julian Soh, Anthony Puca, Mike Manning, and David Gollob. Microsoft azure. _New York, NY, USA:: Apress_ , 2015. 

- [14] Edsger W Dijkstra. Self-stabilizing systems in spite of distributed control. _Communications of the ACM_ , 17(11):643–644, 1974. 

- [15] Shlomi Dolev, Swan Dubois, Maria Potop-Butucaru, and Sébastien Tixeuil. Stabilizing data-link over non-fifo channels with optimal fault-resilience. _Information Processing Letters_ , 111(18):912–920, 2011. 

- [16] Kendric Hood, Joseph Oglio, Mikhail Nesterenko, and Gokarna Sharma. Partitionable asynchronous cryptocurrency blockchain. In _2021 IEEE International Conference on Blockchain and Cryptocurrency (ICBC)_ , pages 1–9. IEEE, 2021. 

- [17] Heidi Howard, Malte Schwarzkopf, Anil Madhavapeddy, and Jon Crowcroft. Raft refloated: Do we have consensus? _ACM SIGOPS Operating Systems Review_ , 49(1):12–21, 2015. 

- [18] Rodney R Howell, Mikhail Nesterenko, and Masaaki Mizuno. Finite-state self-stabilizing protocols in message-passing systems. _Journal of Parallel and Distributed Computing_ , 62(5):792–817, 2002. 

- [19] Boris Koldehofe, Marina Papatriantafilou, and Philippas Tsigas. LYDIAN: an extensible educational animation environment for distributed algorithms. _ACM J. Educ. Resour. Comput._ , 6(2):1:1–1:21, 2006. 

- [20] Leslie Lamport, Robert Shostak, and Marshall Pease. The byzantine generals problem. In _Concurrency: the Works of Leslie Lamport_ , pages 203–226. 2019. 

- [21] Petar Maymounkov and David Mazieres. Kademlia: A peer-to-peer information system based on the xor metric. In _International Workshop on Peer-to-Peer Systems_ , pages 53–65. Springer, 2002. 

- [22] Alberto Montresor and Márk Jelasity. Peersim: A scalable P2P simulator. In Henning Schulzrinne, Karl Aberer, and Anwitaman Datta, editors, _Proceedings P2P 2009, Ninth International Conference on Peer-to-Peer Computing, 9-11 September 2009, Seattle, Washington, USA_ , pages 99–100. IEEE, 2009. 

- [23] Satoshi Nakamoto. Bitcoin: A peer-to-peer electronic cash system. _Decentralized Business Review_ , page 21260, 2008. 

- [24] Shishir Rai, Kendric Hood, Mikhail Nesterenko, and Gokarna Sharma. Blockguard: Adaptive blockchain security. In _2nd International Conference on Blockchain Economics, Security and Protocols_ , page 9, 2021. 

- [25] George F. Riley and Thomas R. Henderson. The _ns-3_ network simulator. In Klaus Wehrle, Mesut Günes, and James Gross, editors, _Modeling and Tools for Network Simulation_ , pages 15–34. Springer, 2010. 

- [26] Ion Stoica, Robert Morris, David Liben-Nowell, David R Karger, M Frans Kaashoek, Frank Dabek, and Hari Balakrishnan. Chord: a scalable peer-to-peer lookup protocol for internet applications. _IEEE/ACM Transactions on networking_ , 11(1):17–32, 2003. 

- [27] Peter Urban, Xavier Défago, and André Schiper. Neko: A single environment to simulate and prototype distributed algorithms. In _Proceedings 15th International Conference on Information Networking_ , pages 503–511. IEEE, 2001. 

- [28] Andras Varga. _OMNeT++_ , pages 35–59. Springer Berlin Heidelberg, Berlin, Heidelberg, 2010. 

- [29] Gavin Wood. Ethereum: A secure decentralized generalized transaction ledger. _Ethereum project yellow paper_ , 151:1–32, 2014. 

