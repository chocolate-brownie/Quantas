#include "quantas/Common/Concrete/Backends/BoostMq/Control/CapacityPreflight.hpp"
#include "quantas/Common/Logger.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace quantas {

unsigned int CapacityPreflight::readSystemCapacity() const {
    std::ifstream input("/proc/sys/fs/mqueue/msg_max");
    unsigned int capacity = 0;
    if (!(input >> capacity)) {
        throw std::runtime_error("cannot read /proc/sys/fs/mqueue/msg_max");
    }
    QUANTAS_LOG_INFO("quantas") << "cat /proc/sys/fs/mqueue/msg_max: " << capacity;
    return capacity;
}

bool CapacityPreflight::promptForTune() const {
    char choice;
    while (true) {
        std::cout << "Would you like to change system fs.mqueue.msg_max (y/n): ";
        if (!(std::cin >> choice)) {
            throw std::runtime_error("failed to read fs.mqueue.msg_max prompt response");
        }

        choice = static_cast<char>(std::tolower(static_cast<unsigned char>(choice)));
        if (choice == 'y') { return true; }
        if (choice == 'n') { return false; }
    }
}

void CapacityPreflight::tuneSystemCapacity(unsigned int requiredCapacity) const {
    const std::string cmd = "sudo sysctl -w fs.mqueue.msg_max=" + std::to_string(requiredCapacity);

    const int ret = std::system(cmd.c_str());
    if (ret != 0) { throw std::runtime_error("failed to update fs.mqueue.msg_max"); }

    std::cout << "fs.mqueue.msg_max changed to: " << requiredCapacity << std::endl;
}

void CapacityPreflight::validateExperimentCapacity(
    const RuntimeExperimentConfig &exp, size_t expIndex, unsigned int &capacity
) const {
    if (exp.initialPeers > 65536) {
        throw std::runtime_error(
            "initialPeers exceeds kernel msg_max hard cap in experiment " + std::to_string(expIndex)
        );
    }

    if (capacity >= static_cast<unsigned int>(exp.initialPeers)) { return; }

    std::cout << "\033[31m" << "BoostMQ control-plane capacity check failed: " << "\033[0m"
              << "peer_count = " << exp.initialPeers << " required_capacity = " << exp.initialPeers
              << " system fs.mqueue.msg_max = " << capacity << std::endl;

    if (!promptForTune()) { throw std::runtime_error("user declined fs.mqueue.msg_max update"); }

    tuneSystemCapacity(static_cast<unsigned int>(exp.initialPeers));
    capacity = static_cast<unsigned int>(exp.initialPeers);
}

bool CapacityPreflight::validate(const std::optional<nlohmann::json> &config) const {
    try {
        if (!config) { throw std::runtime_error("missing runtime config"); }

        unsigned int capacity = readSystemCapacity();
        for (size_t expIndex = 0; expIndex < (*config)["experiments"].size(); ++expIndex) {
            const RuntimeExperimentConfig exp = parseRuntimeExperiment(*config, expIndex);
            validateExperimentCapacity(exp, expIndex, capacity);
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: capacity preflight failed: " << ex.what() << '\n';
        return false;
    }

    return true;
}

} // namespace quantas
