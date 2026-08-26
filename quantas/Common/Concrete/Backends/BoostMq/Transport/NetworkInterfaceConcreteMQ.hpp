#ifndef NETWORK_INTERFACE_CONCRETE_MQ_HPP
#define NETWORK_INTERFACE_CONCRETE_MQ_HPP

#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Metrics/TransportMetrics.hpp"
#include "quantas/Common/Json.hpp"
#include "quantas/Common/NetworkInterface.hpp"
#include <atomic>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <optional>
#include <set>

namespace quantas {

class NetworkInterfaceConcreteMQ : public NetworkInterface {
  private:
    std::atomic<bool> _configured{false};
    std::optional<boost::interprocess::message_queue> _myInbox;
    TransportMetrics _transportMetrics;
    std::size_t _dataSendTimeoutMs{BoostMqQueueConfig::DEFAULT_DATA_SEND_TIMEOUT_MS};

  public:
    NetworkInterfaceConcreteMQ();
    explicit NetworkInterfaceConcreteMQ(interfaceId pubId);
    NetworkInterfaceConcreteMQ(interfaceId pubId, interfaceId internalId);
    ~NetworkInterfaceConcreteMQ() override;

    void
    configure(interfaceId id, std::set<interfaceId> neighbors,
              std::size_t dataSendTimeoutMs = BoostMqQueueConfig::DEFAULT_DATA_SEND_TIMEOUT_MS);

    void unicastTo(nlohmann::json msg, const interfaceId &dest) override;
    void receive() override;

    void clearAll() override;

    void requestStop();

    TransportMetrics transportMetrics() const;
    void resetTransportMetrics();
};
} // namespace quantas

#endif
