#include "../LogWriter.hpp"
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

/*
ConcreteMQ Worker Runtime: two-phase execution model

Phase 1 (setup / runtime assembly)
- Parse CLI and load config.
- Extract one experiment's runtime parameters.
- Configure coordinator context for this experiment.
- Resolve output destination.
- Build local assignments and construct local peers/interfaces.

Phase 2 (execution / lifecycle)
- Run per-round receive + compute for local peers.
- Handle fast-path skip when no runnable local peers.
- Cleanup peer/interface state on success and failure.

This file keeps both phases in one unit for bring-up clarity. Utility
extraction points are grouped below to make later refactoring mechanical.
*/

// --------------------------- Shared data types ---------------------------

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

/*
Reusable utility notes

The helpers below are intentionally written so they can later be moved to a
shared module (e.g. ConcreteMQRuntime.hpp/.cpp) and reused by both:
- concreteSimulationMQ.cpp (worker runtime)
- concreteLeaderMQ.cpp (leader runtime)
*/

/* ========================= Shared utilities ========================= */

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
std::optional<nlohmann::json> loadConfig(const std::string &jsonPath) {
    try {
        std::ifstream inFile(jsonPath);

        if (!inFile.is_open())
            throw std::runtime_error(
                std::string("error: cannot open input file: ") + jsonPath
            );

        nlohmann::json config;
        inFile >> config;

        if (!config.contains("experiments") ||
            !config["experiments"].is_array() ||
            config["experiments"].empty()) {
            throw std::runtime_error(
                "error: configuration missing non-empty 'experiments' array"
            );
        }

        return config;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return std::nullopt;
    }
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

// Resolve and configure the output destination for this experiment.
std::string
configureExperimentOutput(const std::string &logFileBase, size_t expIndex) {
    const std::optional<int> noPort = std::nullopt;
    const std::string metricsFile =
        quantas::makeExperimentFileName(logFileBase, expIndex, noPort, ".json");
    quantas::LogWriter::setLogFile(metricsFile);
    return metricsFile;
}

/* ========================= Worker-only utilities ========================= */

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
void cleanUp(std::vector<quantas::Peer *> &localPeers) {
    for (auto *peer : localPeers) {
        if (!peer) continue;

        peer->clearInterface();
        delete peer;
    }
    localPeers.clear();
}

// Execute the experiment round loop for all local peers.
void runRounds(std::vector<quantas::Peer *> &localPeers, int rounds) {
    quantas::RoundManager::asynchronous();
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

// Collect local assignments owned by this worker.
// Phase-1 behavior: one process owns one peer assignment.
std::vector<MqAssignment> collectLocalAssignments(
    const CliArgs &cli, const ExperimentConfig &exp
) {
    std::vector<MqAssignment> assignments;
    assignments.push_back(buildValidatedLocalAssignment(cli, exp));
    return assignments;
}

// Try to build local peers from topology rules
bool prepareLocalPeers(
    const CliArgs &cli, const ExperimentConfig &exp,
    std::vector<quantas::Peer *> &localPeers
) {
    std::vector<MqAssignment> assignments = collectLocalAssignments(cli, exp);
    if (assignments.empty()) return false;

    localPeers = buildLocalPeers(exp.peerType, assignments);
    return !localPeers.empty();
}

void initializeHooks(
    const nlohmann::json &experiment, std::vector<quantas::Peer *> &localPeers
) {
    if (experiment.contains("parameters")) {
        localPeers.front()->initParameters(
            localPeers, experiment["parameters"]
        );
    }

    const int testsConfigured = experiment.value("tests", 1);
    if (testsConfigured > 1) {
        std::cerr << "warning: concrete MQ mode currently executes a single "
                     "test per experiment.\n";
    }
}

// --------------------------- Worker runtime ---------------------------

int main(int argc, char **argv) {
    auto cli = parseArgs(argc, argv); // CLI input validation
    if (!cli) return 1;

    auto config = loadConfig(cli->jsonPath);
    if (!config) return 1;

    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();

    for (size_t expIndex = 0; expIndex < (*config)["experiments"].size();
         ++expIndex) {
        std::vector<quantas::Peer *> localPeers;
        try {
            /*
            ==================== Phase 1: Setup / Assembly ====================
            Build all runtime state needed to execute this experiment in the
            current worker process.
            */
            const nlohmann::json &experiment =
                (*config)["experiments"].at(expIndex);
            ExperimentConfig exp =
                parseExperiment(*config, expIndex, cli->roundsOverride);

            const std::string logFileBase =
                quantas::chooseLogFileBase(*config, experiment);
            const std::string metricsFile =
                configureExperimentOutput(logFileBase, expIndex);
            coordinator.configureExperiment(
                expIndex, exp.peerType, false, exp.totalPeers, cli->peerId,
                logFileBase, quantas::StopMode::FixedRounds
            );
            std::cout << "[peer " << cli->peerId
                      << "] output file: " << metricsFile << std::endl;
            initRendezvous(coordinator, cli->peerId);

            if (!prepareLocalPeers(*cli, exp, localPeers)) {
                cleanUp(localPeers);
                std::cerr << "experiment " << expIndex
                          << ": no runnable local peers, skipping\n";
                continue;
            }

            initializeHooks(experiment, localPeers);

            /*
            =================== Phase 2: Execute / Cleanup ====================
            Execute rounds for all local peers, then release experiment state.
            */

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
