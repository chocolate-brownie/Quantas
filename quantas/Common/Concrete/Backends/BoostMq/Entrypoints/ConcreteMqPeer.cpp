#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.hpp"
#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/PeerAssignment.hpp"
#include "quantas/Common/LogWriter.hpp"
#include "quantas/Common/Logger.hpp"
#include "quantas/Common/LoggingSupport.hpp"
#include "quantas/Common/Peer.hpp"
#include "quantas/Common/memoryUtil.hpp"
#include <algorithm>
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
    size_t experimentIndex;
    int peerId;
};

using PeerAssignment = quantas::PeerAssignment;

/* The helpers below are intentionally written so they can later be moved to a
   shared runtime module and reused by both:
   - ConcreteMqPeer.cpp (worker runtime)
   - ConcreteMqLeader.cpp (leader runtime) */

// Validate assignment bounds and basic topology invariants.
void validateAssignment(const PeerAssignment& assignment, int totalPeers) {
    if (totalPeers <= 0)
        throw std::runtime_error("error: totalPeers must be > 0");
    if (assignment.id < 0 || assignment.id >= totalPeers)
        throw std::runtime_error("error: assigned peer id " + std::to_string(assignment.id) +
                                 " is outside [0, " + std::to_string(totalPeers - 1) + "]");
    if (assignment.neighbors.find(assignment.id) != assignment.neighbors.end())
        throw std::runtime_error("error: assignment neighbors include self");

    for (const auto neighbor : assignment.neighbors) {
        if (neighbor < 0 || neighbor >= totalPeers)
            throw std::runtime_error("error: neighbor id " + std::to_string(neighbor) +
                                     " is outside [0, " + std::to_string(totalPeers - 1) + "]");
    }
}

// Bind assignment data to the MQ interface, then attach it to the peer.
void applyAssignment(const PeerAssignment& assignment, quantas::NetworkInterfaceConcreteMQ* mq,
                     quantas::Peer* peer) {
    QUANTAS_LOG_INFO("topology") << "peer " << assignment.id
                                 << " using topology=" << assignment.topologyType;
    mq->configure(assignment.id, assignment.neighbors);
    peer->setNetworkInterface(mq);
}

// Parse the launcher-owned experiment index and peer identity.
std::optional<CliArgs> parseArgs(int argc, char** argv) {
    if (argc != 5 || argv == nullptr || std::string(argv[1]) != "--experiment") {
        std::cerr << "Usage: " << argv[0]
                  << " --experiment <experiment_index> <input_json> <peer_id>\n";
        return std::nullopt;
    }

    try {
        CliArgs args;
        size_t parsedCharacters = 0;
        const long long experimentIndex = std::stoll(argv[2], &parsedCharacters);
        if (parsedCharacters != std::string(argv[2]).size() || experimentIndex < 0) {
            throw std::runtime_error("experiment index must be a non-negative integer");
        }

        parsedCharacters = 0;
        const long long peerId = std::stoll(argv[4], &parsedCharacters);
        if (parsedCharacters != std::string(argv[4]).size() || peerId < 0) {
            throw std::runtime_error("peer id must be a non-negative integer");
        }

        args.experimentIndex = static_cast<size_t>(experimentIndex);
        args.jsonPath = argv[3];
        args.peerId = static_cast<int>(peerId);

        return args;
    } catch (const std::exception& ex) {
        std::cerr << "error: invalid CLI arguments: " << ex.what() << '\n';
        return std::nullopt;
    }
}

// Resolve and configure the output destination for this experiment.
std::string configureExperimentOutput(const std::string& logFileBase, size_t expIndex,
                                      int testNumber, std::optional<int> processDisambiguator) {
    const std::string experimentFile =
        quantas::makeExperimentFileName(logFileBase, expIndex, processDisambiguator, ".json");
    const std::string metricsFile =
        quantas::addFileNameSuffix(experimentFile, "_TEST" + std::to_string(testNumber));
    quantas::LogWriter::setLogFile(metricsFile);
    quantas::LogWriter::setTest(0);
    return metricsFile;
}

/* ========================= Worker-only utilities ========================= */

// Perform follower-side start barrier rendezvous.
void initRendezvous(quantas::ProcessCoordinatorMQ& coord, int myId) {
    QUANTAS_LOG_INFO("runner") << "peer " << myId << " configuring process";

    QUANTAS_LOG_INFO("runner") << "peer " << myId << " creating inbox";
    coord.createInbox();

    QUANTAS_LOG_INFO("runner") << "peer " << myId << " sending ready";
    coord.sendReady();
}

/* Construct all peers assigned to this worker and bind each peer to an MQ
   interface configured from its assignment (id + neighbors). */
std::vector<quantas::Peer*> buildLocalPeers(const std::string& peerType,
                                            const std::vector<PeerAssignment>& assignments) {
    std::vector<quantas::Peer*> localPeers;
    localPeers.reserve(assignments.size());

    for (const auto& assignment : assignments) {
        quantas::Peer* peer = quantas::PeerRegistry::makePeer(peerType, assignment.id);
        auto* mq = new quantas::NetworkInterfaceConcreteMQ();
        applyAssignment(assignment, mq, peer);
        localPeers.push_back(peer);
    }

    return localPeers;
}

// Peer clean up
void cleanUp(std::vector<quantas::Peer*>& localPeers) {
    for (auto* peer : localPeers) {
        if (!peer)
            continue;

        peer->clearInterface();
        delete peer;
    }
    localPeers.clear();
}

/* ========================= Rounds Execution Start ========================= */
void runRounds(std::vector<quantas::Peer*>& localPeers, int rounds,
               quantas::ProcessCoordinatorMQ& coordinator) {
    size_t loopCount = 0;
    std::string stopReason = "unknown";
    const auto mode = coordinator.stopMode();
    const char* modeLabel =
        (mode == quantas::StopMode::FixedRounds) ? "FixedRounds" : "DoneSignals";

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
        for (auto* peer : localPeers) {
            if (!peer)
                continue;
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
    for (const auto* peer : localPeers) {
        if (!peer)
            continue;
        QUANTAS_LOG_INFO("runner")
            << "peer " << peer->publicId() << " loop exit summary: mode=" << modeLabel
            << " loopCount=" << loopCount << " currentRoundView=" << currentRound
            << " reason=" << stopReason;
    }
}
/* ========================= Rounds Execution Ends ========================= */

// Try to build peers from topology rules
bool prepareLocalPeers(const quantas::RuntimeExperimentConfig& exp,
                       const std::vector<PeerAssignment>& assignments,
                       std::vector<quantas::Peer*>& localPeers) {
    if (assignments.empty())
        return false;

    for (const auto& assignment : assignments) {
        validateAssignment(assignment, exp.initialPeers);
    }

    localPeers = buildLocalPeers(exp.initialPeerType, assignments);
    return !localPeers.empty();
}

void initializeHooks(const nlohmann::json& experiment, std::vector<quantas::Peer*>& localPeers) {
    const nlohmann::json parameters = experiment.value("parameters", nlohmann::json::object());
    localPeers.front()->initParameters(localPeers, parameters);
}

quantas::TransportMetrics collectTransportMetrics(const std::vector<quantas::Peer*>& localPeers) {
    quantas::TransportMetrics totals;
    for (const auto* peer : localPeers) {
        if (!peer)
            continue;
        const auto* mq =
            dynamic_cast<const quantas::NetworkInterfaceConcreteMQ*>(peer->getNetworkInterface());

        if (!mq)
            continue;

        const auto metrics = mq->transportMetrics();
        totals.sent += metrics.sent;
        totals.receivedRaw += metrics.receivedRaw;
        totals.deliveredToInstream += metrics.deliveredToInstream;
        totals.droppedBackpressure += metrics.droppedBackpressure;
        totals.peakQueueUsage = std::max(totals.peakQueueUsage, metrics.peakQueueUsage);
    }
    return totals;
}

void resetTransportMetrics(const std::vector<quantas::Peer*>& localPeers) {
    for (const auto* peer : localPeers) {
        if (!peer)
            continue;
        auto* mq = dynamic_cast<quantas::NetworkInterfaceConcreteMQ*>(peer->getNetworkInterface());
        if (mq) {
            mq->resetTransportMetrics();
        }
    }
}

void emitFinalExperimentMetrics(const std::chrono::high_resolution_clock::time_point& startTime,
                                const std::vector<quantas::Peer*>& localPeers,
                                std::size_t dataQueueCapacity) {
    const auto endTime = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> duration = endTime - startTime;
    const auto transportMetrics = collectTransportMetrics(localPeers);

    quantas::LogWriter::setValue("RunTime", duration.count());
    quantas::LogWriter::setValue("Peak Memory KB", static_cast<double>(getPeakMemoryKB()));
    quantas::LogWriter::setValue(
        "transportMetrics", quantas::makeTransportMetricsJson(transportMetrics, dataQueueCapacity));
    QUANTAS_LOG_INFO("runner") << "printing output";
    quantas::LogWriter::print();
    QUANTAS_LOG_INFO("runner") << "output printed";
}

// --------------------------- Worker runtime ---------------------------

int main(int argc, char** argv) {
    auto cli = parseArgs(argc, argv); // CLI input validation
    if (!cli)
        return 1;

    auto config = quantas::loadRuntimeConfig(cli->jsonPath);
    if (!config)
        return 1;
    if (cli->experimentIndex >= (*config)["experiments"].size()) {
        std::cerr << "error: experiment index " << cli->experimentIndex << " is out of range\n";
        return 1;
    }

    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();
    const size_t expIndex = cli->experimentIndex;

    std::vector<quantas::Peer*> localPeers;
    int activeTestNumber = 0;
    try {
        /* ==================== Phase 1: Setup / Assembly ====================
        Build all runtime state needed to execute this experiment in the
        current worker process. */
        const nlohmann::json& experiment = (*config)["experiments"].at(expIndex);
        quantas::RuntimeExperimentConfig exp = quantas::parseRuntimeExperiment(*config, expIndex);
        const auto queueConfig = quantas::parseBoostMqConfig(experiment, exp.initialPeers);

        const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);
        const std::optional<int> processDisambiguator = cli->peerId;

        for (int testIndex = 0; testIndex < exp.tests; ++testIndex) {
            const int testNumber = testIndex + 1;
            activeTestNumber = testNumber;

            const std::string metricsFile =
                configureExperimentOutput(logFileBase, expIndex, testNumber, processDisambiguator);
            coordinator.configureExperiment(expIndex, exp.initialPeerType, false, exp.initialPeers,
                                            cli->peerId, logFileBase,
                                            quantas::StopMode::FixedRounds, queueConfig);
            QUANTAS_LOG_INFO("runner") << "peer " << cli->peerId << " output file: " << metricsFile;
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
            QUANTAS_LOG_INFO("runner") << "peer " << cli->peerId << " start signal acknowledged";

            initializeHooks(experiment, localPeers);
            const auto startTime = std::chrono::high_resolution_clock::now();

            /*
               =================== Phase 2: Execute / Cleanup ====================
               Execute rounds for all local peers, then release experiment state.
               */

            try {
                runRounds(localPeers, exp.rounds, coordinator);
            } catch (...) {
                /* Preserve transport evidence collected before a data-send failure, then let the
                 * existing outer handler log the test failure and clean process state. */
                emitFinalExperimentMetrics(startTime, localPeers, queueConfig.dataQueueCapacity);
                throw;
            }
            emitFinalExperimentMetrics(startTime, localPeers, queueConfig.dataQueueCapacity);
            coordinator.notifyPeerStopped(localPeers.front()->publicId());
            coordinator.waitForStop();

            cleanUp(localPeers);
        }
        coordinator.cleanUp();
    } catch (const std::exception& ex) {
        if (activeTestNumber > 0) {
            try {
                coordinator.notifyPeerFailed(cli->peerId);
            } catch (const std::exception& notifyError) {
                QUANTAS_LOG_ERROR("runner")
                    << "peer " << cli->peerId
                    << " could not report failure to leader: " << notifyError.what();
            }
        }
        cleanUp(localPeers);
        coordinator.cleanUp();
        QUANTAS_LOG_ERROR("runner")
            << "experiment " << expIndex
            << (activeTestNumber > 0 ? " test " + std::to_string(activeTestNumber) : "")
            << " failed: " << ex.what();
        return 1;
    }

    return 0;
}
