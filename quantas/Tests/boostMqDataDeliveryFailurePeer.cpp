#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Logging/BoostMqOutputPaths.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.hpp"
#include "quantas/Common/Concrete/Runtime/Metrics/TransportMetrics.hpp"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr int kTotalPeers = 2;
constexpr quantas::interfaceId kSenderId = 0;
constexpr quantas::interfaceId kBlockedReceiverId = 1;

std::string metricsPath(quantas::interfaceId peerId) {
    return quantas::makeBoostMqPeerOutputPath("build/tests/mqDataDeliveryFailure.txt", 0, peerId,
                                              1);
}

void writeMetrics(quantas::interfaceId peerId, const quantas::TransportMetrics& metrics) {
    std::ofstream output(metricsPath(peerId));
    if (!output.is_open())
        throw std::runtime_error("could not write test peer metrics");

    output << nlohmann::json{
        {"transportMetrics", quantas::makeTransportMetricsJson(metrics, 1)}};
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc != 5 || std::string(argv[1]) != "--experiment")
        return 2;

    const auto peerId = static_cast<quantas::interfaceId>(std::stoi(argv[4]));
    quantas::BoostMqQueueConfig queueConfig;
    queueConfig.controlQueueCapacity = kTotalPeers;
    queueConfig.dataQueueCapacity = 1;
    queueConfig.controlSendTimeoutMs = 1000;

    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();
    try {
        coordinator.configureExperiment(0, "ExamplePeer", false, kTotalPeers, peerId,
                                        "build/tests/mqDataDeliveryFailure.txt",
                                        quantas::StopMode::FixedRounds, queueConfig);
        coordinator.createInbox();

        if (peerId == kBlockedReceiverId) {
            boost::interprocess::message_queue dataQueue(
                boost::interprocess::open_only, "peer_1_data");
            const char filler = 0;
            dataQueue.send(&filler, sizeof(filler), 0);
        }

        coordinator.sendReady();
        (void)coordinator.waitForAssignments();
        coordinator.waitForStart();

        if (peerId == kBlockedReceiverId) {
            coordinator.waitForStop();
            coordinator.cleanUp();
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        quantas::NetworkInterfaceConcreteMQ networkInterface;
        networkInterface.configure(kSenderId, {kBlockedReceiverId});

        try {
            networkInterface.unicastTo(nlohmann::json{{"message", "force backpressure"}},
                                       kBlockedReceiverId);
        } catch (const std::runtime_error&) {
            writeMetrics(peerId, networkInterface.transportMetrics());
            coordinator.notifyPeerFailed(peerId);
            coordinator.cleanUp();
            return 1;
        }
    } catch (...) {
        coordinator.cleanUp();
        return 2;
    }

    coordinator.cleanUp();
    return 3;
}
