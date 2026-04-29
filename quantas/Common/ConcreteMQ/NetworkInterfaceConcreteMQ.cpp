#include "NetworkInterfaceConcreteMQ.hpp"
#include "../Packet.hpp"
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <sstream>
#include <vector>

using namespace boost::interprocess;

namespace quantas {

NetworkInterfaceConcreteMQ::NetworkInterfaceConcreteMQ() = default;

NetworkInterfaceConcreteMQ::NetworkInterfaceConcreteMQ(interfaceId pubId)
    : NetworkInterfaceConcreteMQ(pubId, pubId) {}

NetworkInterfaceConcreteMQ::NetworkInterfaceConcreteMQ(interfaceId pubId, interfaceId internalId)
    : NetworkInterface(pubId, internalId) {}

NetworkInterfaceConcreteMQ::~NetworkInterfaceConcreteMQ() { clearAll(); }

void NetworkInterfaceConcreteMQ::configure(interfaceId id, std::set<interfaceId> neighbors) {
    _publicId = id;
    _internalId = id;
    _neighbors = neighbors;

    std::string queueName = "peer_" + std::to_string(id);

    /* WARNING: capacity 10 is capped by the POSIX limit fs.mqueue.msg_max
    (default 10 on Linux). Algorithms with more peers/traffic (PBFT, Bitcoin,
    Kademlia) will need `sudo sysctl fs.mqueue.msg_max=<higher>` raised
    before this queue can be created with a larger size */
    _myInbox.emplace(boost::interprocess::open_only, queueName.c_str());
    _configured = true;
}

/* Steps: unicastTo(json msg, dest)
  → build Packet (source, target, msg)
  → write Packet to boost::archive::binary_oarchive backed by std::stringstream
  → send stringstream's bytes over MQ */
void NetworkInterfaceConcreteMQ::unicastTo(nlohmann::json msg, const interfaceId &dest) {
    if (_neighbors.find(dest) == _neighbors.end())
        return;

    Packet p;
    p.setSource(publicId());
    p.setTarget(dest);
    p.setMessage(msg);

    std::stringstream ss;
    boost::archive::binary_oarchive oa(ss);
    oa << p; // calls boost serialization 'save' machinery defined in Packet.hpp

    std::string bytes = ss.str();

    try {
        std::string queueName = "peer_" + std::to_string(dest);
        message_queue mq(open_only, queueName.c_str());
        if (bytes.size() > mq.get_max_msg_size())
            throw std::runtime_error(
                "unicastTo: packet to peer_" + std::to_string(dest) + " is " +
                std::to_string(bytes.size()) + " bytes, exceeds queue limit of " +
                std::to_string(mq.get_max_msg_size())
            );

        mq.send(bytes.data(), bytes.size(), 0);
    } catch (const interprocess_exception &ex) {
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

        while (_myInbox->try_receive(buffer.data(), buffer.size(), recvd_size, priority)) {
            std::stringstream ss(std::string(buffer.data(), recvd_size));
            boost::archive::binary_iarchive ia(ss);
            Packet p;
            ia >> p;
            std::lock_guard<std::mutex> lock(_inStream_mtx);
            _inStream.push_back(std::move(p));
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(std::string("receive: ") + ex.what());
    }
}

void NetworkInterfaceConcreteMQ::clearAll() { NetworkInterface::clearAll(); }

} // namespace quantas
