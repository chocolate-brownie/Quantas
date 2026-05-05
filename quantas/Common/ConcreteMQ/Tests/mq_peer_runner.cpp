#include "../../Peer.hpp"
#include "../NetworkInterfaceConcreteMQ.hpp"
#include "../ProcessCoordinatorMQ.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

nlohmann::json checkJsonFile(const char *path) {
    std::ifstream inFile(path);

    if (!inFile.is_open()) {
        throw std::runtime_error(std::string("error: cannot open input file: ") + path);
    }

    nlohmann::json config;
    inFile >> config;

    return config;
}

void initRendezvous(quantas::ProcessCoordinatorMQ &coord, int myId, int N) {
    std::cout << "[peer " << myId << "] Configuring process" << std::endl;
    coord.configureProcess(false, N, myId);

    std::cout << "[peer " << myId << "] Creating inboxes" << std::endl;
    coord.createInbox();

    std::cout << "[peer " << myId << "] Sending ready" << std::endl;
    coord.sendReady();

    std::cout << "[peer " << myId << "] Waiting for start" << std::endl;
    coord.waitForStart();

    std::cout << "[peer " << myId << "] Rendezvous done" << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_json> <peer_id> [rounds]\n";
        return 1;
    }

    nlohmann::json config = checkJsonFile(argv[1]);
    nlohmann::json experiment = config["experiments"][0];

    int myId = std::stoi(argv[2]);
    int N = experiment["topology"]["initialPeers"];
    std::string peerType = experiment["topology"]["initialPeerType"];
    int rounds = argc >= 4 ? std::stoi(argv[3]) : static_cast<int>(experiment["rounds"]);

    auto &coord = quantas::ProcessCoordinatorMQ::instance();
    initRendezvous(coord, myId, N);

    quantas::Peer *peer = quantas::PeerRegistry::makePeer(peerType, myId);
    auto *mq = new quantas::NetworkInterfaceConcreteMQ();

    std::set<quantas::interfaceId> neighbors;
    for (int other = 0; other < N; ++other) {
        if (other != myId)
            neighbors.insert(other);
    }

    mq->configure(myId, neighbors);
    peer->setNetworkInterface(mq);

    quantas::RoundManager::setCurrentRound(0);
    quantas::RoundManager::setLastRound(rounds);

    for (int i = 0; i < rounds; ++i) {
        quantas::RoundManager::incrementRound();
        peer->receive();
        peer->tryPerformComputation();
    }

    peer->clearInterface();
    delete peer;

    // The leader owns MQ cleanup in this smoke test. If a peer removes its own
    // queue immediately, the other peer may still be sending algorithm packets.
    return 0;
}
