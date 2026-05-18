#include "../LoggingSupport.hpp"
#include "ProcessCoordinatorMQ.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

struct ExperimentConfig {
    int initialPeers{0};
    std::string initialPeerType;
};

ExperimentConfig parseLeaderExp(const nlohmann::json &config, size_t expIndex) {
    const nlohmann::json experiment = config["experiments"].at(expIndex);
    if (!experiment.contains("topology"))
        throw std::runtime_error("error: experiment missing 'topology'");

    ExperimentConfig out;
    out.initialPeers = experiment["topology"].value("initialPeers", 0);
    out.initialPeerType = experiment["topology"].value("initialPeerType", "");

    if (out.initialPeers <= 0) throw std::runtime_error("error: topology.initialPeers must be > 0");
    if (out.initialPeerType.empty())
        throw std::runtime_error("error: topology.initialPeerType is empty");
    return out;
}

std::optional<nlohmann::json> parseAndLoadConfig(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_json>\n";
        return std::nullopt;
    }

    try {
        std::ifstream inFile(argv[1]);
        if (!inFile.is_open())
            throw std::runtime_error(std::string("error: cannot open input file: ") + argv[1]);

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

/* --------------------------- Leader runtime --------------------------- */
int main(int argc, char *argv[]) {
    /* Parse cli args and load the json config file for the leader process */
    auto config = parseAndLoadConfig(argc, argv);
    if (!config) return 1;

    /* ProcessCoordinatorMQ` is the component that owns the rendezvous protocol API
     * (`createBarrier`, `waitForAllReady`, `broadcastStart`, `configureExperiment`). */
    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();

    /* ==================== Phase 1: Setup ====================
     * Build all runtime state needed to execute this experiment in the current leader
     * process.

     For each experiment:
     - parse `initialPeers`, `initialPeerType`, `rounds` (if needed),
     - compute `logFileBase` using `chooseLogFileBase(...)`,
     - call `configureExperiment(..., isLeader=true, totalPeers=N, ...)`,
    - run start gate: createBarrier -> waitForAllReady -> broadcastStart. */

    for (size_t expIndex = 0; expIndex < (*config)["experiments"].size(); ++expIndex) {
        try {
            const nlohmann::json &experiment = (*config)["experiments"].at(expIndex);
            ExperimentConfig exp = parseLeaderExp(*config, expIndex);

            const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);

            coordinator.configureExperiment(
                expIndex, exp.initialPeerType, true, exp.initialPeers, quantas::NO_PEER_ID,
                logFileBase, quantas::StopMode::FixedRounds
            );
            coordinator.createBarrier();
            coordinator.waitForAllReady();
            coordinator.broadcastStart();
        } catch (const std::exception &ex) {
            std::cerr << "error: leader failed at experiment " << expIndex << ": " << ex.what()
                      << '\n';
            return 1;
        }
    }

    return 0;
}
