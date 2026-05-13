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
#include <vector>

// --------------------------- CLI/config data ---------------------------

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

// Build the local phase-1 assignment (single peer per process).
MqAssignment
buildLocalAssignment(const CliArgs &cli, const ExperimentConfig &exp) {
    MqAssignment a;
    a.id = cli.peerId;

    for (int other = 0; other < exp.totalPeers; ++other) {
        if (other != cli.peerId) a.neighbors.insert(other);
    }
    return a;
}

// Validate assignment bounds and basic topology invariants.
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

// Bind assignment data to the MQ interface, then attach it to the peer.
void applyAssignment(
    const MqAssignment &assignment, quantas::NetworkInterfaceConcreteMQ *mq,
    quantas::Peer *peer
) {
    mq->configure(assignment.id, assignment.neighbors);
    peer->setNetworkInterface(mq);
}

// Convenience wrapper for local assignment build + validation.
MqAssignment
buildValidatedLocalAssignment(const CliArgs &cli, const ExperimentConfig &exp) {
    MqAssignment assignment = buildLocalAssignment(cli, exp);
    validateAssignment(assignment, exp.totalPeers);
    return assignment;
}

// Parse worker CLI arguments: input JSON, peer id, optional rounds override.
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

// Load root configuration and validate experiments array exists.
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

// Extract one experiment's runtime parameters for this worker.
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

// Perform follower-side start barrier rendezvous.
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

/* Construct all peers assigned to this worker and bind each peer to an MQ
    interface configured from its assignment (id + neighbors). */
std::vector<quantas::Peer *> buildLocalPeers(
    const std::string &peerType, const std::vector<MqAssignment> &assignments
) {
    std::vector<quantas::Peer *> localPeers;
    localPeers.reserve(assignments.size());

    for (const auto &assignment : assignments) {
        quantas::Peer *peer =
            quantas::PeerRegistry::makePeer(peerType, assignment.id);
        auto *mq = new quantas::NetworkInterfaceConcreteMQ();
        applyAssignment(assignment, mq, peer);
        localPeers.push_back(peer);
    }

    return localPeers;
}

// Peer clean up
void cleanUp(
    std::vector<quantas::Peer *> &localPeers
) {
    for (auto *peer : localPeers) {
        if (!peer) continue;

        peer->clearInterface();
        delete peer;
    }
    localPeers.clear();
}

// Execute the experiment round loop for all local peers.
void runRounds(std::vector<quantas::Peer *> &localPeers, int rounds) {
    quantas::RoundManager::setCurrentRound(0);
    quantas::RoundManager::setLastRound(rounds);
    for (int i = 0; i < rounds; ++i) {
        quantas::RoundManager::incrementRound();
        for (auto *peer : localPeers) {
            if (!peer) continue;
            peer->receive();
            peer->tryPerformComputation();
        }
    }
}

// --------------------------- Worker runtime ---------------------------

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
        std::vector<quantas::Peer *> localPeers;
        try {
            const nlohmann::json &experiment = config["experiments"].at(expIndex);
            ExperimentConfig exp =
                parseExperiment(config, expIndex, cli->roundsOverride);

            const std::string logFileBase =
                quantas::chooseLogFileBase(config, experiment);
            coordinator.configureExperiment(
                expIndex, exp.peerType, false, exp.totalPeers, cli->peerId,
                logFileBase, quantas::StopMode::FixedRounds
            );
            initRendezvous(coordinator, cli->peerId);

            // Build local peers from topology rules
            std::vector<MqAssignment> assignments;
            assignments.push_back(buildValidatedLocalAssignment(*cli, exp));
            localPeers = buildLocalPeers(exp.peerType, assignments);

            // Execute the experiment round loop for all local peers.
            runRounds(localPeers, exp.rounds);

            cleanUp(localPeers);
        } catch (const std::exception &ex) {
            cleanUp(localPeers);
            std::cerr << "error: experiment " << expIndex
                      << " failed: " << ex.what() << '\n';
            return 1;
        }
    }

    return 0;
}
