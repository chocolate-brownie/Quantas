#include "../LoggingSupport.hpp"
#include "../Peer.hpp"
#include "NetworkInterfaceConcreteMQ.hpp"
#include "ProcessCoordinatorMQ.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

struct CliArgs {
    std::string jsonPath;
    int peerId;
    std::optional<int> roundsOverride;
};

struct ExperimentConfig {
    int totalPeers{0};
    std::string peerType;
    int rounds{0};
};

struct MqAssignment {
    quantas::interfaceId id{quantas::NO_PEER_ID};
    std::set<quantas::interfaceId> neighbors;
};

MqAssignment
buildLocalAssignment(const CliArgs &cli, const ExperimentConfig &exp) {
    MqAssignment a;
    a.id = cli.peerId;

    for (int other = 0; other < exp.totalPeers; ++other) {
        if (other != cli.peerId) a.neighbors.insert(other);
    }
    return a;
}

void validateAssignment(const MqAssignment &assignment, int totalPeers) {
    if (totalPeers <= 0)
        throw std::runtime_error("error: totalPeers must be > 0");
    if (assignment.id < 0 || assignment.id >= totalPeers)
        throw std::runtime_error(
            "error: assigned peer id " + std::to_string(assignment.id) +
            " is outside [0, " + std::to_string(totalPeers - 1) + "]"
        );
    if (assignment.neighbors.find(assignment.id) != assignment.neighbors.end())
        throw std::runtime_error("error: assignment neighbors include self");

    for (const auto neighbor : assignment.neighbors) {
        if (neighbor < 0 || neighbor >= totalPeers)
            throw std::runtime_error(
                "error: neighbor id " + std::to_string(neighbor) +
                " is outside [0, " + std::to_string(totalPeers - 1) + "]"
            );
    }
}

void applyAssignment(
    const MqAssignment &assignment, quantas::NetworkInterfaceConcreteMQ *mq,
    quantas::Peer *peer
) {
    mq->configure(assignment.id, assignment.neighbors);
    peer->setNetworkInterface(mq);
}

std::optional<CliArgs> parseArgs(int argc, char **argv) {
    if (argc < 3 || argv == nullptr) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_json> <peer_id> [rounds]\n";
        return std::nullopt;
    }

    try {
        CliArgs args;
        args.jsonPath = argv[1];
        args.peerId = std::stoi(argv[2]);

        if (argc >= 4) args.roundsOverride = std::stoi(argv[3]);

        return args;
    } catch (const std::exception &ex) {
        std::cerr << "error: invalid CLI arguments: " << ex.what() << '\n';
        return std::nullopt;
    }
}

nlohmann::json loadConfig(const std::string &jsonPath) {
    std::ifstream inFile(jsonPath);

    if (!inFile.is_open())
        throw std::runtime_error(
            std::string("error: cannot open input file: ") + jsonPath
        );

    nlohmann::json config;
    inFile >> config;

    if (!config.contains("experiments") || !config["experiments"].is_array() ||
        config["experiments"].empty()) {
        throw std::runtime_error(
            "error: configuration missing non-empty 'experiments' array"
        );
    }

    return config;
}

ExperimentConfig parseExperiment(
    const nlohmann::json &config, size_t expIndex,
    const std::optional<int> &roundsOverride
) {
    const nlohmann::json experiment = config["experiments"].at(expIndex);
    if (!experiment.contains("topology"))
        throw std::runtime_error("error: experiment missing 'topology'");

    ExperimentConfig out;
    out.totalPeers = experiment["topology"].value("initialPeers", 0);
    out.peerType = experiment["topology"].value("initialPeerType", "");
    out.rounds = roundsOverride.has_value()
                     ? *roundsOverride
                     : static_cast<int>(experiment.value("rounds", 0));

    if (out.totalPeers <= 0)
        throw std::runtime_error("error: topology.initialPeers must be > 0");
    if (out.peerType.empty())
        throw std::runtime_error("error: topology.initialPeerType is empty");
    if (out.rounds <= 0) throw std::runtime_error("error: rounds must be > 0");

    return out;
}

void initRendezvous(quantas::ProcessCoordinatorMQ &coord, int myId) {
    std::cout << "[peer " << myId << "] Configuring process" << std::endl;

    std::cout << "[peer " << myId << "] Creating inboxes" << std::endl;
    coord.createInbox();

    std::cout << "[peer " << myId << "] Sending ready" << std::endl;
    coord.sendReady();

    std::cout << "[peer " << myId << "] Waiting for start" << std::endl;
    coord.waitForStart();

    std::cout << "[peer " << myId << "] Rendezvous done" << std::endl;
}

int main(int argc, char **argv) {
    auto cli = parseArgs(argc, argv); // CLI input validation
    if (!cli) return 1;

    nlohmann::json config;
    try {
        config = loadConfig(cli->jsonPath);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    // Process synchronization
    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();

    for (size_t expIndex = 0; expIndex < config["experiments"].size();
         ++expIndex) {
        const nlohmann::json &experiment = config["experiments"].at(expIndex);

        ExperimentConfig exp;
        try {
            exp = parseExperiment(config, expIndex, cli->roundsOverride);
        } catch (const std::exception &ex) {
            std::cerr << ex.what() << '\n';
            return 1;
        }

        const std::string logFileBase =
            quantas::chooseLogFileBase(config, experiment);
        coordinator.configureExperiment(
            expIndex, exp.peerType, false, exp.totalPeers, cli->peerId,
            logFileBase, quantas::StopMode::FixedRounds
        );
        initRendezvous(coordinator, cli->peerId);

        // Peer construction
        quantas::Peer *peer =
            quantas::PeerRegistry::makePeer(exp.peerType, cli->peerId);
        auto *mq = new quantas::NetworkInterfaceConcreteMQ();

        // Build neighbours from topology rules
        auto assignment = buildLocalAssignment(*cli, exp);
        validateAssignment(assignment, exp.totalPeers);
        applyAssignment(assignment, mq, peer);

        // Run rounds
        quantas::RoundManager::setCurrentRound(0);
        quantas::RoundManager::setLastRound(exp.rounds);
        for (int i = 0; i < exp.rounds; ++i) {
            quantas::RoundManager::incrementRound();
            peer->receive();
            peer->tryPerformComputation();
        }

        peer->clearInterface();
        delete peer;
    }

    return 0;
}
