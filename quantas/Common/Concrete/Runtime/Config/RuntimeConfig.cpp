#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace quantas {

std::optional<nlohmann::json> loadRuntimeConfig(const std::string &jsonPath) {
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

RuntimeExperimentConfig parseRuntimeExperiment(
    const nlohmann::json &config, size_t expIndex, const std::optional<int> &roundsOverride
) {
    const nlohmann::json experiment = config["experiments"].at(expIndex);
    if (!experiment.contains("topology"))
        throw std::runtime_error("error: experiment missing 'topology'");

    RuntimeExperimentConfig out;
    out.initialPeers = experiment["topology"].value("initialPeers", 0);
    out.initialPeerType = experiment["topology"].value("initialPeerType", "");
    out.topology = experiment["topology"];
    out.rounds = roundsOverride.has_value() ? *roundsOverride
                                            : static_cast<int>(experiment.value("rounds", 0));
    out.tests = experiment.value("tests", 1);
    out.doneTimeoutMs = experiment.value("doneTimeoutMs", 30000);

    if (out.initialPeers <= 0) throw std::runtime_error("error: topology.initialPeers must be > 0");
    if (out.initialPeerType.empty())
        throw std::runtime_error("error: topology.initialPeerType is empty");
    if (out.tests <= 0) throw std::runtime_error("error: tests must be > 0");
    if (out.rounds <= 0) throw std::runtime_error("error: rounds must be > 0");
    if (out.doneTimeoutMs <= 0) throw std::runtime_error("error: doneTimeoutMs must be > 0");

    return out;
}

} // namespace quantas
