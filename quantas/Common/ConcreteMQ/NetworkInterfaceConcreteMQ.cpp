#include "NetworkInterfaceConcreteMQ.hpp"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include "../Packet.hpp"
#include <string>

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
  → serialize Packet to JSON string
  → send string bytes over MQ */
void NetworkInterfaceConcreteMQ::unicastTo(nlohmann::json msg, const interfaceId &dest) {
    if (_neighbors.find(dest) == _neighbors.end())
        return;
}

/* Steps: receive()
  → try_receive() raw bytes from MQ
  → parse bytes back to JSON
  → reconstruct Packet from JSON
  → push Packet into _inStream */
void NetworkInterfaceConcreteMQ::receive() {}

} // namespace quantas
