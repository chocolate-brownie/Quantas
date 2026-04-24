#include "NetworkInterfaceConcrete.hpp"
#include <deque>
#include "ProcessCoordinator.hpp"

namespace quantas {

NetworkInterfaceConcrete::NetworkInterfaceConcrete() = default;

NetworkInterfaceConcrete::NetworkInterfaceConcrete(interfaceId pubId)
    : NetworkInterface(pubId, pubId) {}

NetworkInterfaceConcrete::NetworkInterfaceConcrete(interfaceId pubId, interfaceId internalId)
    : NetworkInterface(pubId, internalId) {}

NetworkInterfaceConcrete::~NetworkInterfaceConcrete() {
    clearAll();
}

void NetworkInterfaceConcrete::configure(const ProcessCoordinator::PeerAssignment& assignment) {
    _publicId = assignment.id;
    _internalId = assignment.id;
    _neighbors = assignment.neighbors;
    ProcessCoordinator::instance().registerInterface(_publicId, this);
    _configured = true;
}

void NetworkInterfaceConcrete::unicastTo(nlohmann::json msg, const interfaceId& dest) {
    if (!_configured.load()) return;
    ProcessCoordinator::instance().unicast(_publicId, dest, msg);
}

void NetworkInterfaceConcrete::receive() {
    if (!_configured.load()) return;
    std::deque<Packet> buffer;
    ProcessCoordinator::instance().drainInbound(_publicId, buffer);
    if (buffer.empty()) return;
    std::lock_guard<std::mutex> lock(_inStream_mtx);
    while (!buffer.empty()) {
        _inStream.push_back(std::move(buffer.front()));
        buffer.pop_front();
    }
}

void NetworkInterfaceConcrete::clearAll() {
    if (!_configured.exchange(false)) {
        return;
    }
    ProcessCoordinator::instance().unregisterInterface(_publicId);
    NetworkInterface::clearAll();
}

void NetworkInterfaceConcrete::requestStop() {
    if (!_configured.load()) return;
    ProcessCoordinator::instance().notifyPeerStopped(_publicId);
}

} // namespace quantas
