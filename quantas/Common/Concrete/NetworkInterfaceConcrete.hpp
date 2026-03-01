#ifndef QUANTAS_NETWORK_INTERFACE_CONCRETE_HPP
#define QUANTAS_NETWORK_INTERFACE_CONCRETE_HPP

#include <atomic>
#include <set>
#include <vector>

#include "../NetworkInterface.hpp"
#include "ProcessCoordinator.hpp"

namespace quantas {

class NetworkInterfaceConcrete : public NetworkInterface {
public:
    NetworkInterfaceConcrete();
    explicit NetworkInterfaceConcrete(interfaceId pubId);
    NetworkInterfaceConcrete(interfaceId pubId, interfaceId internalId);
    ~NetworkInterfaceConcrete() override;

    void configure(const ProcessCoordinator::PeerAssignment& assignment);

    void unicastTo(nlohmann::json msg, const interfaceId& dest) override;
    void receive() override;

    void clearAll() override;

    void requestStop();

private:
    std::atomic<bool> _configured{false};
};

} // namespace quantas

#endif // QUANTAS_NETWORK_INTERFACE_CONCRETE_HPP
