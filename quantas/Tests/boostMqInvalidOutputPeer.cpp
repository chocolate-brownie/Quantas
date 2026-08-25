#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Logging/BoostMqOutputPaths.hpp"
#include <fstream>
#include <string>

namespace {
std::string outputPath(const std::string& base, int peerId) {
    return quantas::makeBoostMqPeerOutputPath(base, 0, peerId, 1);
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc != 5 || std::string(argv[1]) != "--experiment")
        return 2;

    const std::string inputPath = argv[3];
    const int peerId = std::stoi(argv[4]);
    quantas::BoostMqQueueConfig queueConfig;
    queueConfig.controlQueueCapacity = 2;
    queueConfig.dataQueueCapacity = 1;

    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();
    try {
        coordinator.configureExperiment(0, "ExamplePeer", false, 2, peerId,
                                        "build/tests/mqInvalidOutput.txt",
                                        quantas::StopMode::FixedRounds, queueConfig);
        coordinator.createInbox();
        coordinator.sendReady();
        (void)coordinator.waitForAssignments();
        coordinator.waitForStart();

        const std::string path = outputPath("build/tests/mqInvalidOutput.txt", peerId);
        if (peerId == 0) {
            std::ofstream output(path);
            output
                << R"({"transportMetrics":{"sent":0,"received_raw":0,"delivered_to_instream":0,"dropped_backpressure":0}})";
        } else if (inputPath.find("Malformed") != std::string::npos) {
            std::ofstream output(path);
            output << "{ malformed";
        }

        coordinator.notifyPeerStopped(peerId);
        coordinator.waitForStop();
        coordinator.cleanUp();
        return 0;
    } catch (...) {
        coordinator.cleanUp();
        return 1;
    }
}
