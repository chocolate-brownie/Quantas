#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>

#include "../LoggingSupport.hpp"
#include "../Logger.hpp"
#include "../OutputWriter.hpp"
#include "../Peer.hpp"
#include "../RoundManager.hpp"
#include "../memoryUtil.hpp"
#include "NetworkInterfaceConcrete.hpp"
#include "ProcessCoordinator.hpp"

namespace quantas {

namespace {

Peer* instantiatePeer(const std::string& peerType, interfaceId id) {
    std::string concreteType = peerType + "Concrete";
    try {
        return PeerRegistry::makePeer(concreteType, id);
    } catch (const std::exception&) {
        return PeerRegistry::makePeer(peerType, id);
    }
}

} // namespace

} // namespace quantas

using namespace quantas;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ./quantas.exe <config.json> [port]" << std::endl;
        return 1;
    }

    const std::string configPath = argv[1];
    std::optional<int> explicitPort;
    if (argc >= 3) {
        explicitPort = std::stoi(argv[2]);
    }

    std::ifstream inFile(configPath);
    if (!inFile.is_open()) {
        std::cerr << "error: cannot open input file: " << configPath << std::endl;
        return 1;
    }

    nlohmann::json config;
    inFile >> config;
    inFile.close();

    if (!config.contains("experiments") || !config["experiments"].is_array()) {
        std::cerr << "error: configuration missing experiments array." << std::endl;
        return 1;
    }

    auto& coordinator = ProcessCoordinator::instance();

    for (size_t expIndex = 0; expIndex < config["experiments"].size(); ++expIndex) {
        const nlohmann::json& experiment = config["experiments"][expIndex];

        if (!experiment.contains("topology")) {
            std::cerr << "experiment missing topology description, skipping." << std::endl;
            continue;
        }

        const std::string peerType = experiment["topology"].value("initialPeerType", "");
        if (peerType.empty()) {
            std::cerr << "experiment missing initialPeerType, skipping." << std::endl;
            continue;
        }

        const std::string logFileBase = chooseLogFileBase(config, experiment);

        coordinator.configureProcess(config,
                                     experiment,
                                     peerType,
                                     0,
                                     explicitPort.has_value(),
                                     explicitPort,
                                     expIndex,
                                     logFileBase);

        auto assignments = coordinator.waitForAssignments();

        std::vector<Peer*> localPeers;
        localPeers.reserve(assignments.size());
        for (const auto& assignment : assignments) {
            Peer* peer = nullptr;
            try {
                peer = instantiatePeer(peerType, assignment.id);
            } catch (const std::exception& ex) {
                std::cerr << "failed to create peer of type " << peerType
                          << " for id " << assignment.id << ": " << ex.what() << std::endl;
                continue;
            }
            auto* iface = dynamic_cast<NetworkInterfaceConcrete*>(peer->getNetworkInterface());
            if (!iface) {
                std::cerr << "peer " << assignment.id << " lacks concrete network interface." << std::endl;
                delete peer;
                continue;
            }
            iface->configure(assignment);
            localPeers.push_back(peer);
        }

        std::optional<int> portForLog;
        if (explicitPort.has_value()) {
            portForLog = explicitPort;
        }
        if (!localPeers.empty()) {
            const auto& endpoints = coordinator.endpoints();
            auto endpointIt = endpoints.find(localPeers.front()->publicId());
            if (endpointIt != endpoints.end() && endpointIt->second.port > 0) {
                portForLog = endpointIt->second.port;
            }
        }

        const std::string metricsFile = makeExperimentFileName(logFileBase, expIndex, portForLog, ".txt");
        OutputWriter::setLogFile(metricsFile);

        QUANTAS_LOG_INFO("runner") << "starting experiment " << expIndex;
        QUANTAS_LOG_INFO("runner") << "received " << assignments.size() << " assignments.";

        if (assignments.empty() || localPeers.empty()) {
            for (Peer* peer : localPeers) {
                delete peer;
            }
            coordinator.cleanupExperiment();
            continue;
        }

        if (experiment.contains("parameters")) {
            localPeers.front()->initParameters(localPeers, experiment["parameters"]);
        }

        const int testsConfigured = experiment.value("tests", 1);
        if (testsConfigured > 1) {
            QUANTAS_LOG_WARN("runner") << "concrete mode currently executes a single test per experiment.";
        }

        const auto startTime = std::chrono::high_resolution_clock::now();

        OutputWriter::instance()->setTest(0);
        RoundManager::asynchronous();
        RoundManager::setCurrentRound(0);
        coordinator.markReady();
        coordinator.waitForStartSignal();
        QUANTAS_LOG_INFO("runner") << "start signal acknowledged.";
        while (!coordinator.shouldStop()) {
            for (Peer* peer : localPeers) {
                if (!peer->isCrashed()) {
                    peer->receive();
                    peer->tryPerformComputation();
                }
            }
            localPeers.front()->endOfRound(localPeers);
        }

        localPeers.front()->endOfExperiment(localPeers);
        
        QUANTAS_LOG_INFO("runner") << "stop detected, waiting for confirmation.";

        coordinator.waitForStop();
        QUANTAS_LOG_INFO("runner") << "stop confirmed, finalising.";

        const auto endTime = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double> duration = endTime - startTime;
        OutputWriter::setValue("RunTime", duration.count());
        OutputWriter::setValue("Peak Memory KB", static_cast<double>(getPeakMemoryKB()));
        QUANTAS_LOG_INFO("runner") << "printing output";
        OutputWriter::print();
        QUANTAS_LOG_INFO("runner") << "output printed";

        for (Peer* peer : localPeers) {
            auto* iface = dynamic_cast<NetworkInterfaceConcrete*>(peer->getNetworkInterface());
            if (iface) {
                iface->clearAll();
            }
            peer->clearInterface();
            delete peer;
        }
        localPeers.clear();

        coordinator.cleanupExperiment();
        QUANTAS_LOG_INFO("runner") << "experiment complete";
    }

    coordinator.shutdown();

    QUANTAS_LOG_INFO("runner") << "exiting";
    return 0;
}
