#include "../LogWriter.hpp"
#include "../Logger.hpp"
#include "../LoggingSupport.hpp"
#include "../Peer.hpp"
#include "../memoryUtil.hpp"
#include "NetworkInterfaceConcreteMQ.hpp"
#include "ProcessCoordinatorMQ.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
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
    nlohmann::json topology;
};

struct MqAssignment {
    quantas::interfaceId id{quantas::NO_PEER_ID};
    std::set<quantas::interfaceId> neighbors;
};

struct TopologyResult {
    std::vector<MqAssignment> assignments;
};

/* The helpers below are intentionally written so they can later be moved to a
   shared module (e.g. ConcreteMQRuntime.hpp/.cpp) and reused by both:
   - ConcreteMqPeer.cpp (worker runtime)
   - ConcreteMqLeader.cpp (leader runtime) */

/* =================== Topology Configuration Start ========================= */
TopologyResult buildTopology(const nlohmann::json &topology) {
    TopologyResult result;

    const int initialPeers = topology.value("initialPeers", 0);

    if (initialPeers <= 0) { return result; }

    result.assignments.resize(static_cast<size_t>(initialPeers));

    std::vector<quantas::interfaceId> ids(static_cast<size_t>(initialPeers));
    std::iota(ids.begin(), ids.end(), 0);

    if (topology.value("identifiers", "") == "random") {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::shuffle(ids.begin(), ids.end(), rng);
    }

    auto addUndirectedEdge = [&](quantas::interfaceId a, quantas::interfaceId b) {
        if (a == b || a < 0 || b < 0 || a >= initialPeers || b >= initialPeers) return;
        result.assignments[static_cast<size_t>(a)].id = a;
        result.assignments[static_cast<size_t>(b)].id = b;
        result.assignments[static_cast<size_t>(a)].neighbors.insert(b);
        result.assignments[static_cast<size_t>(b)].neighbors.insert(a);
    };

    auto addDirectedEdge = [&](quantas::interfaceId from, quantas::interfaceId to) {
        if (from < 0 || to < 0 || from >= initialPeers || to >= initialPeers) return;
        result.assignments[static_cast<size_t>(from)].id = from;
        result.assignments[static_cast<size_t>(from)].neighbors.insert(to);
    };

    const std::string type = topology.value("type", "");
    if (type == "complete") {
        for (int i = 0; i < initialPeers; ++i) {
            for (int j = i + 1; j < initialPeers; ++j) {
                quantas::interfaceId a = ids[static_cast<size_t>(i)];
                quantas::interfaceId b = ids[static_cast<size_t>(j)];
                addUndirectedEdge(a, b);
            }
        }
    } else if (type == "star") {
        for (int i = 1; i < initialPeers; ++i) {
            quantas::interfaceId center = ids[0];
            quantas::interfaceId leaf = ids[static_cast<size_t>(i)];
            addUndirectedEdge(center, leaf);
        }
    } else if (type == "grid") {
        int height = topology.value("height", 1);
        int width = topology.value("width", 1);
        if (height * width != initialPeers) {
            width = initialPeers;
            height = 1;
        }
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                int idx = i * width + j;
                quantas::interfaceId current = ids[static_cast<size_t>(idx)];
                if (j + 1 < width) {
                    quantas::interfaceId right = ids[static_cast<size_t>(idx + 1)];
                    addUndirectedEdge(current, right);
                }
                if (i + 1 < height) {
                    quantas::interfaceId down = ids[static_cast<size_t>(idx + width)];
                    addUndirectedEdge(current, down);
                }
            }
        }
    } else if (type == "torus") {
        int height = topology.value("height", 1);
        int width = topology.value("width", 1);
        if (height * width != initialPeers) {
            width = initialPeers;
            height = 1;
        }
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                int idx = i * width + j;
                quantas::interfaceId current = ids[static_cast<size_t>(idx)];
                quantas::interfaceId right =
                    ids[static_cast<size_t>(i * width + ((j + 1) % width))];
                quantas::interfaceId down =
                    ids[static_cast<size_t>(((i + 1) % height) * width + j)];
                addUndirectedEdge(current, right);
                addUndirectedEdge(current, down);
            }
        }
    } else if (type == "chain") {
        for (int i = 0; i < initialPeers - 1; ++i) {
            quantas::interfaceId a = ids[static_cast<size_t>(i)];
            quantas::interfaceId b = ids[static_cast<size_t>(i + 1)];
            addUndirectedEdge(a, b);
        }
    } else if (type == "ring") {
        for (int i = 0; i < initialPeers; ++i) {
            quantas::interfaceId a = ids[static_cast<size_t>(i)];
            quantas::interfaceId b = ids[static_cast<size_t>((i + 1) % initialPeers)];
            addUndirectedEdge(a, b);
        }
    } else if (type == "unidirectionalRing") {
        for (int i = 0; i < initialPeers; ++i) {
            quantas::interfaceId a = ids[static_cast<size_t>(i)];
            quantas::interfaceId b = ids[static_cast<size_t>((i + 1) % initialPeers)];
            addDirectedEdge(a, b);
        }
    } else if (type == "userList") {
        const auto it = topology.find("list");
        if (it != topology.end() && it->is_object()) {
            for (int i = 0; i < initialPeers; ++i) {
                quantas::interfaceId id = ids[static_cast<size_t>(i)];
                result.assignments[static_cast<size_t>(id)].id = id;
            }
            for (const auto &[key, value] : it->items()) {
                int idx = std::stoi(key);
                if (idx < 0 || idx >= initialPeers) continue;
                quantas::interfaceId src = ids[static_cast<size_t>(idx)];
                if (!value.is_array()) continue;
                for (const auto &destValue : value) {
                    int neighborIndex = destValue.get<int>();
                    if (neighborIndex < 0 || neighborIndex >= initialPeers) continue;
                    quantas::interfaceId dest = ids[static_cast<size_t>(neighborIndex)];
                    addDirectedEdge(src, dest);
                }
            }
        }
    } else {
        // default: fully disconnected but ensure ids set
        for (quantas::interfaceId id : ids) { result.assignments[static_cast<size_t>(id)].id = id; }
    }

    // ensure ids assigned even if no edges
    for (quantas::interfaceId id = 0; id < initialPeers; ++id) {
        result.assignments[static_cast<size_t>(id)].id = id;
    }

    return result;
}
/* =================== Topology Configuration End =========================== */

// Build the neighbors according to the topology. "for peer X, who should X know?"
MqAssignment buildLocalAssignment(const CliArgs &cli, const ExperimentConfig &exp) {
    TopologyResult topology = buildTopology(exp.topology);

    if (cli.peerId < 0 || cli.peerId >= static_cast<int>(topology.assignments.size())) {
        throw std::runtime_error(
            "error: peer id " + std::to_string(cli.peerId) + " has no topology assignment"
        );
    }

    MqAssignment assignment = topology.assignments[static_cast<size_t>(cli.peerId)];
    std::ostringstream neighbors;
    bool first = true;
    for (const auto neighbor : assignment.neighbors) {
        if (!first) neighbors << ',';
        neighbors << neighbor;
        first = false;
    }
    QUANTAS_LOG_INFO("topology")
        << "peer " << assignment.id << " neighbors=[" << neighbors.str() << "]";

    return assignment;
}

// Validate assignment bounds and basic topology invariants.
void validateAssignment(const MqAssignment &assignment, int totalPeers) {
    if (totalPeers <= 0) throw std::runtime_error("error: totalPeers must be > 0");
    if (assignment.id < 0 || assignment.id >= totalPeers)
        throw std::runtime_error(
            "error: assigned peer id " + std::to_string(assignment.id) + " is outside [0, " +
            std::to_string(totalPeers - 1) + "]"
        );
    if (assignment.neighbors.find(assignment.id) != assignment.neighbors.end())
        throw std::runtime_error("error: assignment neighbors include self");

    for (const auto neighbor : assignment.neighbors) {
        if (neighbor < 0 || neighbor >= totalPeers)
            throw std::runtime_error(
                "error: neighbor id " + std::to_string(neighbor) + " is outside [0, " +
                std::to_string(totalPeers - 1) + "]"
            );
    }
}

// Bind assignment data to the MQ interface, then attach it to the peer.
void applyAssignment(
    const MqAssignment &assignment, quantas::NetworkInterfaceConcreteMQ *mq, quantas::Peer *peer
) {
    mq->configure(assignment.id, assignment.neighbors);
    peer->setNetworkInterface(mq);
}

// Convenience wrapper for local assignment build + validation.
MqAssignment buildValidatedLocalAssignment(const CliArgs &cli, const ExperimentConfig &exp) {
    MqAssignment assignment = buildLocalAssignment(cli, exp);
    validateAssignment(assignment, exp.totalPeers);
    return assignment;
}

// Parse worker CLI arguments: input JSON, peer id, optional rounds override.
std::optional<CliArgs> parseArgs(int argc, char **argv) {
    if (argc < 3 || argv == nullptr) {
        std::cerr << "Usage: " << argv[0] << " <input_json> <peer_id> [rounds]\n";
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
            throw std::runtime_error(std::string("error: cannot open input file: ") + jsonPath);

        nlohmann::json config;
        inFile >> config;

        if (!config.contains("experiments") || !config["experiments"].is_array() ||
            config["experiments"].empty()) {
            throw std::runtime_error("error: configuration missing non-empty 'experiments' array");
        }

        return config;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return std::nullopt;
    }
}

// Extract one experiment's runtime parameters for this worker.
ExperimentConfig parseExperiment(
    const nlohmann::json &config, size_t expIndex, const std::optional<int> &roundsOverride
) {
    const nlohmann::json experiment = config["experiments"].at(expIndex);
    if (!experiment.contains("topology"))
        throw std::runtime_error("error: experiment missing 'topology'");

    ExperimentConfig out;
    out.totalPeers = experiment["topology"].value("initialPeers", 0);
    out.peerType = experiment["topology"].value("initialPeerType", "");
    out.rounds = roundsOverride.has_value() ? *roundsOverride
                                            : static_cast<int>(experiment.value("rounds", 0));
    out.topology = experiment["topology"];

    if (out.totalPeers <= 0) throw std::runtime_error("error: topology.initialPeers must be > 0");
    if (out.peerType.empty()) throw std::runtime_error("error: topology.initialPeerType is empty");
    if (out.rounds <= 0) throw std::runtime_error("error: rounds must be > 0");

    return out;
}

// Resolve and configure the output destination for this experiment.
std::string configureExperimentOutput(
    const std::string &logFileBase, size_t expIndex, std::optional<int> processDisambiguator
) {
    const std::string metricsFile =
        quantas::makeExperimentFileName(logFileBase, expIndex, processDisambiguator, ".json");
    quantas::LogWriter::setLogFile(metricsFile);
    return metricsFile;
}

/* ========================= Worker-only utilities ========================= */

// Perform follower-side start barrier rendezvous.
void initRendezvous(quantas::ProcessCoordinatorMQ &coord, int myId) {
    QUANTAS_LOG_INFO("runner") << "peer " << myId << " configuring process";

    QUANTAS_LOG_INFO("runner") << "peer " << myId << " creating inbox";
    coord.createInbox();

    QUANTAS_LOG_INFO("runner") << "peer " << myId << " sending ready";
    coord.sendReady();

    QUANTAS_LOG_INFO("runner") << "peer " << myId << " waiting for start";
    coord.waitForStart();

    QUANTAS_LOG_INFO("runner") << "peer " << myId << " start signal acknowledged";
}

/* Construct all peers assigned to this worker and bind each peer to an MQ
    interface configured from its assignment (id + neighbors). */
std::vector<quantas::Peer *>
buildLocalPeers(const std::string &peerType, const std::vector<MqAssignment> &assignments) {
    std::vector<quantas::Peer *> localPeers;
    localPeers.reserve(assignments.size());

    for (const auto &assignment : assignments) {
        quantas::Peer *peer = quantas::PeerRegistry::makePeer(peerType, assignment.id);
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

/* ========================= Rounds Execution Start ========================= */
void runRounds(
    std::vector<quantas::Peer *> &localPeers, int rounds, quantas::ProcessCoordinatorMQ &coordinator
) {
    quantas::RoundManager::asynchronous();
    quantas::RoundManager::setCurrentRound(0);
    quantas::RoundManager::setLastRound(rounds);

    size_t loopCount = 0;
    std::string stopReason = "unknown";
    const auto mode = coordinator.stopMode();
    const char *modeLabel =
        (mode == quantas::StopMode::FixedRounds) ? "FixedRounds" : "DoneSignals";

    while (!coordinator.shouldStop()) {
        if (mode == quantas::StopMode::FixedRounds && loopCount >= static_cast<size_t>(rounds)) {
            stopReason = "fixed_rounds_reached";
            break;
        }

        // --------------------------- Round starts ---------------------------
        quantas::RoundManager::incrementRound();
        for (auto *peer : localPeers) {
            if (!peer) continue;
            peer->receive();
            peer->tryPerformComputation();
        }
        localPeers.front()->endOfRound(localPeers);
        ++loopCount;
        // --------------------------- Round Ends ---------------------------

        if (mode == quantas::StopMode::DoneSignals && loopCount >= static_cast<size_t>(rounds)) {
            stopReason = "done_signals_not_implemented_fallback";
            coordinator.requestStop(stopReason);
        }
    }

    /* If the algorithm layer has an `endOfExperiment` it will override otherwise the runtime layer
     * does nothing */
    localPeers.front()->endOfExperiment(localPeers);

    coordinator.notifyPeerStopped(localPeers.front()->publicId());
    coordinator.waitForStop();

    // Observability/debug evidence for how and why each peer’s loop terminated.
    const auto currentRound = quantas::RoundManager::currentRound();
    for (const auto *peer : localPeers) {
        if (!peer) continue;
        QUANTAS_LOG_INFO("runner")
            << "peer " << peer->publicId() << " loop exit summary: mode=" << modeLabel
            << " loopCount=" << loopCount << " currentRoundView=" << currentRound
            << " reason=" << stopReason;
    }
}
/* ========================= Rounds Execution Ends ========================= */

/* Collect local assignments owned by this worker.
Phase-1 behavior: one process owns one peer assignment */
std::vector<MqAssignment> collectLocalAssignments(const CliArgs &cli, const ExperimentConfig &exp) {
    std::vector<MqAssignment> assignments;
    assignments.push_back(buildValidatedLocalAssignment(cli, exp));
    return assignments;
}

// Try to build local peers from topology rules
bool prepareLocalPeers(
    const CliArgs &cli, const ExperimentConfig &exp, std::vector<quantas::Peer *> &localPeers
) {
    std::vector<MqAssignment> assignments = collectLocalAssignments(cli, exp);
    if (assignments.empty()) return false;

    localPeers = buildLocalPeers(exp.peerType, assignments);
    return !localPeers.empty();
}

void initializeHooks(const nlohmann::json &experiment, std::vector<quantas::Peer *> &localPeers) {
    if (experiment.contains("parameters")) {
        localPeers.front()->initParameters(localPeers, experiment["parameters"]);
    }

    const int testsConfigured = experiment.value("tests", 1);
    if (testsConfigured > 1) {
        QUANTAS_LOG_WARN("runner")
            << "concrete MQ mode currently executes a single test per experiment";
    }
}

void emitFinalExperimentMetrics(const std::chrono::high_resolution_clock::time_point &startTime) {
    const auto endTime = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> duration = endTime - startTime;
    quantas::LogWriter::setValue("RunTime", duration.count());
    quantas::LogWriter::setValue("Peak Memory KB", static_cast<double>(getPeakMemoryKB()));
    QUANTAS_LOG_INFO("runner") << "printing output";
    quantas::LogWriter::print();
    QUANTAS_LOG_INFO("runner") << "output printed";
}

// --------------------------- Worker runtime ---------------------------

int main(int argc, char **argv) {
    auto cli = parseArgs(argc, argv); // CLI input validation
    if (!cli) return 1;

    auto config = loadConfig(cli->jsonPath);
    if (!config) return 1;

    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();

    for (size_t expIndex = 0; expIndex < (*config)["experiments"].size(); ++expIndex) {
        std::vector<quantas::Peer *> localPeers;
        try {
            /*
            ==================== Phase 1: Setup / Assembly ====================
            Build all runtime state needed to execute this experiment in the
            current worker process.
            */
            const nlohmann::json &experiment = (*config)["experiments"].at(expIndex);
            ExperimentConfig exp = parseExperiment(*config, expIndex, cli->roundsOverride);

            const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);
            const std::optional<int> processDisambiguator = cli->peerId;
            const std::string metricsFile =
                configureExperimentOutput(logFileBase, expIndex, processDisambiguator);
            coordinator.configureExperiment(
                expIndex, exp.peerType, false, exp.totalPeers, cli->peerId, logFileBase,
                quantas::StopMode::FixedRounds
            );
            QUANTAS_LOG_INFO("runner") << "peer " << cli->peerId << " output file: " << metricsFile;
            initRendezvous(coordinator, cli->peerId);

            if (!prepareLocalPeers(*cli, exp, localPeers)) {
                cleanUp(localPeers);
                coordinator.cleanUp();
                QUANTAS_LOG_WARN("runner")
                    << "experiment " << expIndex << ": no runnable local peers, skipping";
                continue;
            }

            initializeHooks(experiment, localPeers);
            const auto startTime = std::chrono::high_resolution_clock::now();

            /*
            =================== Phase 2: Execute / Cleanup ====================
            Execute rounds for all local peers, then release experiment state.
            */

            runRounds(localPeers, exp.rounds, coordinator);
            emitFinalExperimentMetrics(startTime);

            cleanUp(localPeers);
            coordinator.cleanUp();
        } catch (const std::exception &ex) {
            cleanUp(localPeers);
            coordinator.cleanUp();
            QUANTAS_LOG_ERROR("runner") << "experiment " << expIndex << " failed: " << ex.what();
            return 1;
        }
    }

    return 0;
}
