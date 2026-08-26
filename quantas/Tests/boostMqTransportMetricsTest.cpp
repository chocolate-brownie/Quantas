#include "quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.hpp"
#include "quantas/Common/Packet.hpp"
#include <boost/archive/binary_oarchive.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cassert>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

int main() {
    using boost::interprocess::create_only;
    using boost::interprocess::message_queue;

    constexpr std::size_t queueCapacity = 4;
    constexpr std::size_t maxMessageSize = 4096;
    const quantas::interfaceId peerId = static_cast<quantas::interfaceId>(::getpid());
    const std::string queueName = "peer_" + std::to_string(peerId) + "_data";
    const quantas::interfaceId blockedPeerId = peerId + 1;
    const std::string blockedQueueName =
        "peer_" + std::to_string(blockedPeerId) + "_data";

    message_queue::remove(queueName.c_str());
    message_queue::remove(blockedQueueName.c_str());
    message_queue queue(create_only, queueName.c_str(), queueCapacity, maxMessageSize);

    quantas::Packet packet(0, 1, nlohmann::json{{"message", "test"}});
    packet.setSendTime();
    std::stringstream stream;
    boost::archive::binary_oarchive archive(stream);
    archive << packet;
    const std::string bytes = stream.str();

    queue.send(bytes.data(), bytes.size(), 0);
    queue.send(bytes.data(), bytes.size(), 0);

    quantas::NetworkInterfaceConcreteMQ networkInterface;
    networkInterface.configure(peerId, {});
    networkInterface.receive();

    const nlohmann::json expectedMessage{{"message", "test"}};
    for (int i = 0; i < 2; ++i) {
        assert(!networkInterface.inStreamEmpty());
        const quantas::Packet received = networkInterface.popInStream();
        assert(received.sourceId() == 1);
        assert(received.targetId() == 0);
        assert(received.getMessage() == expectedMessage);
    }
    assert(networkInterface.inStreamEmpty());

    const quantas::TransportMetrics metrics = networkInterface.transportMetrics();
    assert(metrics.peakQueueUsage == 2);
    assert(metrics.receivedRaw == 2);
    assert(metrics.deliveredToInstream == 2);

    const nlohmann::json report = quantas::makeTransportMetricsJson(metrics, queueCapacity);
    assert(report.at("data_queue_capacity") == queueCapacity);
    assert(report.at("peak_observed_queue_usage") == 2);

    message_queue blockedQueue(create_only, blockedQueueName.c_str(), 1, maxMessageSize);
    blockedQueue.send(bytes.data(), bytes.size(), 0);
    constexpr std::size_t dataSendTimeoutMs = 25;
    networkInterface.configure(peerId, {blockedPeerId}, dataSendTimeoutMs);

    const auto sendStart = std::chrono::steady_clock::now();
    std::string sendError;
    try {
        networkInterface.unicastTo(nlohmann::json{{"message", "blocked"}}, blockedPeerId);
    } catch (const std::runtime_error& ex) {
        sendError = ex.what();
    }
    const auto sendDuration = std::chrono::steady_clock::now() - sendStart;

    assert(!sendError.empty());
    assert(sendError.find("peer " + std::to_string(peerId)) != std::string::npos);
    assert(sendError.find("peer " + std::to_string(blockedPeerId)) != std::string::npos);
    assert(sendError.find("after 25 ms") != std::string::npos);
    assert(sendDuration < std::chrono::seconds(1));
    assert(networkInterface.transportMetrics().droppedBackpressure == 1);

    message_queue::remove(queueName.c_str());
    message_queue::remove(blockedQueueName.c_str());
    return 0;
}
