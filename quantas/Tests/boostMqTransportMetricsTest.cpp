#include "quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.hpp"
#include "quantas/Common/Packet.hpp"
#include <boost/archive/binary_oarchive.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cassert>
#include <sstream>
#include <string>
#include <unistd.h>

int main() {
    using boost::interprocess::create_only;
    using boost::interprocess::message_queue;

    constexpr std::size_t queueCapacity = 4;
    constexpr std::size_t maxMessageSize = 4096;
    const quantas::interfaceId peerId = static_cast<quantas::interfaceId>(::getpid());
    const std::string queueName = "peer_" + std::to_string(peerId) + "_data";

    message_queue::remove(queueName.c_str());
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

    const quantas::TransportMetrics metrics = networkInterface.transportMetrics();
    assert(metrics.peakQueueUsage == 2);
    assert(metrics.receivedRaw == 2);
    assert(metrics.deliveredToInstream == 2);

    const nlohmann::json report = quantas::makeTransportMetricsJson(metrics, queueCapacity);
    assert(report.at("data_queue_capacity") == queueCapacity);
    assert(report.at("peak_observed_queue_usage") == 2);

    message_queue::remove(queueName.c_str());
    return 0;
}
