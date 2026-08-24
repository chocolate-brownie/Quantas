#include "quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.hpp"
#include "quantas/Common/Logger.hpp"
#include "quantas/Common/Packet.hpp"
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <vector>

using namespace boost::interprocess;

namespace quantas {
namespace {
std::string peerDataQueueName(interfaceId id) {
    return "peer_" + std::to_string(id) + "_data";
}
} // namespace

NetworkInterfaceConcreteMQ::NetworkInterfaceConcreteMQ() = default;

NetworkInterfaceConcreteMQ::NetworkInterfaceConcreteMQ(interfaceId pubId)
    : NetworkInterfaceConcreteMQ(pubId, pubId) {}

NetworkInterfaceConcreteMQ::NetworkInterfaceConcreteMQ(interfaceId pubId, interfaceId internalId)
    : NetworkInterface(pubId, internalId) {}

NetworkInterfaceConcreteMQ::~NetworkInterfaceConcreteMQ() {
    clearAll();
}

void NetworkInterfaceConcreteMQ::configure(interfaceId id, std::set<interfaceId> neighbors) {
    _publicId = id;
    _internalId = id;
    _neighbors = neighbors;

    std::ostringstream neighborList;
    bool first = true;
    for (const auto neighbor : _neighbors) {
        if (!first) neighborList << ',';
        neighborList << neighbor;
        first = false;
    }
    QUANTAS_LOG_INFO("topology") << "configured MQ peer " << _publicId << " neighbors=["
                                 << neighborList.str() << "]";

    std::string queueName = peerDataQueueName(id);

    // ProcessCoordinatorMQ creates the data queue before this interface opens it.
    _myInbox.emplace(boost::interprocess::open_only, queueName.c_str());
    _configured = true;
}

/* Steps: unicastTo(json msg, dest)
  → build Packet (source, target, msg)
  → write Packet to boost::archive::binary_oarchive backed by std::stringstream
  → send stringstream's bytes over MQ */
void NetworkInterfaceConcreteMQ::unicastTo(nlohmann::json msg, const interfaceId& dest) {
    if (_neighbors.find(dest) == _neighbors.end()) return;
    Packet p;
    p.setSource(publicId());
    p.setTarget(dest);
    p.setMessage(msg);

    std::stringstream ss;
    boost::archive::binary_oarchive oa(ss);
    p.setSendTime(); // set time
    oa << p;         // calls boost serialization 'save' machinery defined in Packet.hpp

    std::string bytes = ss.str();

    try {
        std::string queueName = peerDataQueueName(dest);
        message_queue mq(open_only, queueName.c_str());
        if (bytes.size() > mq.get_max_msg_size())
            throw std::runtime_error(
                "unicastTo: packet to peer_" + std::to_string(dest) + " is " +
                std::to_string(bytes.size()) + " bytes, exceeds queue limit of " +
                std::to_string(mq.get_max_msg_size())
            );

        /* Bound the send so a full destination queue fails the test instead of
         * blocking the peer forever. */
        constexpr int dataSendTimeoutMs = 5;
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(dataSendTimeoutMs);

        const bool sent = mq.timed_send(bytes.data(), bytes.size(), 0, deadline);

        if (!sent) {
            ++_transportMetrics.droppedBackpressure;
            throw std::runtime_error("peer " + std::to_string(publicId()) +
                                     " timed out sending data to peer " + std::to_string(dest) +
                                     " after " + std::to_string(dataSendTimeoutMs) + " ms");
        }

        _transportMetrics.sent++;

    } catch (const interprocess_exception& ex) {
        throw std::runtime_error(
            "unicastTo: failed to open peer_" + std::to_string(dest) + " queue: " + ex.what()
        );
    }
}

/* Steps: receive()
  → try_receive() raw bytes from MQ into a char buffer
  → wrap bytes in std::stringstream
  → read Packet via boost::archive::binary_iarchive
  → push Packet into _inStream */
void NetworkInterfaceConcreteMQ::receive() {
    try {
        std::vector<char> buffer(_myInbox->get_max_msg_size());
        unsigned int priority;
        message_queue::size_type recvd_size;

        // Largest queue size we observed so far
        const auto currentUsage = _myInbox->get_num_msg();

        // How many messages are waiting right now
        _transportMetrics.peakQueueUsage = std::max(_transportMetrics.peakQueueUsage, currentUsage);

        while (_myInbox->try_receive(buffer.data(), buffer.size(), recvd_size, priority)) {
            _transportMetrics.receivedRaw++;

            std::stringstream ss(std::string(buffer.data(), recvd_size));
            boost::archive::binary_iarchive ia(ss);
            Packet p;
            ia >> p;

            // Compute latency
            auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now().time_since_epoch()
            )
                             .count();

            std::int64_t latency = nowNs - p.getSendTime();
            std::cout << "latency peer_" << p.sourceId() << " -> peer_" << publicId() << ": "
                      << latency << " ns\n";

            std::lock_guard<std::mutex> lock(_inStream_mtx);
            _inStream.push_back(std::move(p));
            _transportMetrics.deliveredToInstream++;
        }
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error(std::string("receive: ") + ex.what());
    }
}

void NetworkInterfaceConcreteMQ::clearAll() {
    NetworkInterface::clearAll();
}

TransportMetrics NetworkInterfaceConcreteMQ::transportMetrics() const {
    return _transportMetrics;
}

void NetworkInterfaceConcreteMQ::resetTransportMetrics() {
    _transportMetrics = {};
}

} // namespace quantas
