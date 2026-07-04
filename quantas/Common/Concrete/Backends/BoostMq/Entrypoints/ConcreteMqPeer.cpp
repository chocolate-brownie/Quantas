#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.hpp"
#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/PeerAssignment.hpp"
#include "quantas/Common/LogWriter.hpp"
#include "quantas/Common/Logger.hpp"
#include "quantas/Common/LoggingSupport.hpp"
#include "quantas/Common/Peer.hpp"
#include "quantas/Common/memoryUtil.hpp"
#include <chrono>
#include <iostream>
#include <optional>
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

using PeerAssignment = quantas::PeerAssignment;

/* The helpers below are intentionally written so they can later be moved to a
   shared runtime module and reused by both:
   - ConcreteMqPeer.cpp (worker runtime)
   - ConcreteMqLeader.cpp (leader runtime) */

// Validate assignment bounds and basic topology invariants.
void validateAssignment(const PeerAssignment &assignment, int totalPeers) {
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
    const PeerAssignment &assignment, quantas::NetworkInterfaceConcreteMQ *mq, quantas::Peer *peer
) {
    QUANTAS_LOG_INFO("topology") << "peer " << assignment.id
                                 << " using topology=" << assignment.topologyType;
    mq->configure(assignment.id, assignment.neighbors);
    peer->setNetworkInterface(mq);
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

// Resolve and configure the output destination for this experiment.
std::string configureExperimentOutput(
    const std::string &logFileBase, size_t expIndex, int testNumber,
    std::optional<int> processDisambiguator
) {
    const std::string experimentFile = quantas::makeExperimentFileName(
        logFileBase,
        expIndex,
        processDisambiguator,
        ".json"
    );
    const std::string metricsFile = quantas::addFileNameSuffix(
        experimentFile,
        "_TEST" + std::to_string(testNumber)
    );
    quantas::LogWriter::setLogFile(metricsFile);
    quantas::LogWriter::setTest(0);
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
}

/* Construct all peers assigned to this worker and bind each peer to an MQ
   interface configured from its assignment (id + neighbors). */
std::vector<quantas::Peer *>
buildLocalPeers(const std::string &peerType, const std::vector<PeerAssignment> &assignments) {
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
    size_t loopCount = 0;
    std::string stopReason = "unknown";
    const auto mode = coordinator.stopMode();
    const char *modeLabel = (mode == quantas::StopMode::FixedRounds) ? "FixedRounds"
                                                                     : "DoneSignals";

    if (mode == quantas::StopMode::FixedRounds) {
        quantas::RoundManager::synchronous();
    } else {
        quantas::RoundManager::asynchronous();
    }
    quantas::RoundManager::setCurrentRound(0);
    quantas::RoundManager::setLastRound(rounds);

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

// Try to build peers from topology rules
bool prepareLocalPeers(
    const quantas::RuntimeExperimentConfig &exp, const std::vector<PeerAssignment> &assignments,
    std::vector<quantas::Peer *> &localPeers
) {
    if (assignments.empty()) return false;

    for (const auto &assignment : assignments) { validateAssignment(assignment, exp.initialPeers); }

    localPeers = buildLocalPeers(exp.initialPeerType, assignments);
    return !localPeers.empty();
}

void initializeHooks(const nlohmann::json &experiment, std::vector<quantas::Peer *> &localPeers) {
    if (experiment.contains("parameters")) {
        localPeers.front()->initParameters(localPeers, experiment["parameters"]);
    }
}

quantas::TransportMetrics collectTransportMetrics(const std::vector<quantas::Peer *> &localPeers) {
    quantas::TransportMetrics totals;
    for (const auto *peer : localPeers) {
        if (!peer) continue;
        const auto *mq = dynamic_cast<const quantas::NetworkInterfaceConcreteMQ *>(
            peer->getNetworkInterface()
        );
        if (!mq) continue;

        const auto metrics = mq->transportMetrics();
        totals.sent += metrics.sent;
        totals.receivedRaw += metrics.receivedRaw;
        totals.deliveredToInstream += metrics.deliveredToInstream;
        totals.droppedBackpressure += metrics.droppedBackpressure;
    }
    return totals;
}

void resetTransportMetrics(const std::vector<quantas::Peer *> &localPeers) {
    for (const auto *peer : localPeers) {
        if (!peer) continue;
        auto *mq = dynamic_cast<quantas::NetworkInterfaceConcreteMQ *>(peer->getNetworkInterface());
        if (mq) { mq->resetTransportMetrics(); }
    }
}

void emitFinalExperimentMetrics(
    const std::chrono::high_resolution_clock::time_point &startTime,
    const std::vector<quantas::Peer *> &localPeers
) {
    const auto endTime = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> duration = endTime - startTime;
    const auto transportMetrics = collectTransportMetrics(localPeers);

    quantas::LogWriter::setValue("RunTime", duration.count());
    quantas::LogWriter::setValue("Peak Memory KB", static_cast<double>(getPeakMemoryKB()));
    quantas::LogWriter::setValue(
        "transportMetrics",
        nlohmann::json{
            {"sent", transportMetrics.sent},
            {"received_raw", transportMetrics.receivedRaw},
            {"delivered_to_instream", transportMetrics.deliveredToInstream},
            {"dropped_backpressure", transportMetrics.droppedBackpressure}
        }
    );
    QUANTAS_LOG_INFO("runner") << "printing output";
    quantas::LogWriter::print();
    QUANTAS_LOG_INFO("runner") << "output printed";
}

// --------------------------- Worker runtime ---------------------------

int main(int argc, char **argv) {
    auto cli = parseArgs(argc, argv); // CLI input validation
    if (!cli) return 1;

    auto config = quantas::loadRuntimeConfig(cli->jsonPath);
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
            quantas::RuntimeExperimentConfig exp = quantas::parseRuntimeExperiment(
                *config,
                expIndex,
                cli->roundsOverride
            );

            const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);
            const std::optional<int> processDisambiguator = cli->peerId;

            for (int testIndex = 0; testIndex < exp.tests; ++testIndex) {
                const int testNumber = testIndex + 1;
                const std::string metricsFile = configureExperimentOutput(
                    logFileBase,
                    expIndex,
                    testNumber,
                    processDisambiguator
                );
                coordinator.configureExperiment(
                    expIndex,
                    exp.initialPeerType,
                    false,
                    exp.initialPeers,
                    cli->peerId,
                    logFileBase,
                    quantas::StopMode::FixedRounds
                );
                QUANTAS_LOG_INFO("runner")
                    << "peer " << cli->peerId << " output file: " << metricsFile;
                QUANTAS_LOG_INFO("runner") << "peer " << cli->peerId << " starting experiment "
                                           << expIndex << " test " << testNumber;

                initRendezvous(coordinator, cli->peerId);

                std::vector<PeerAssignment> assignments = coordinator.waitForAssignments();
                if (!prepareLocalPeers(exp, assignments, localPeers)) {
                    cleanUp(localPeers);
                    coordinator.cleanUp();
                    QUANTAS_LOG_WARN("runner")
                        << "experiment " << expIndex << ": no runnable local peers, skipping";
                    continue;
                }
                resetTransportMetrics(localPeers);

                QUANTAS_LOG_INFO("runner") << "peer " << cli->peerId << " waiting for start";
                coordinator.waitForStart();
                QUANTAS_LOG_INFO("runner")
                    << "peer " << cli->peerId << " start signal acknowledged";

                initializeHooks(experiment, localPeers);
                const auto startTime = std::chrono::high_resolution_clock::now();

                /*
                   =================== Phase 2: Execute / Cleanup ====================
                   Execute rounds for all local peers, then release experiment state.
                   */

                runRounds(localPeers, exp.rounds, coordinator);
                emitFinalExperimentMetrics(startTime, localPeers);
                coordinator.notifyPeerStopped(localPeers.front()->publicId());
                coordinator.waitForStop();

                cleanUp(localPeers);
            }
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
