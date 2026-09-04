#ifndef QUANTAS_BOOST_MQ_QUEUE_CONFIG_HPP
#define QUANTAS_BOOST_MQ_QUEUE_CONFIG_HPP

#include "quantas/Common/Concrete/Runtime/Topology/PeerAssignment.hpp"
#include "quantas/Common/Json.hpp"
#include <cstddef>
#include <vector>

namespace quantas {

struct BoostMqQueueConfig {
    // Maximum number of algorithm messages that one peer data queue can hold.
    static constexpr std::size_t DEFAULT_DATA_QUEUE_CAPACITY = 1000;

    // Maximum serialized message size accepted by a peer data queue, in bytes.
    static constexpr std::size_t DEFAULT_MAX_MESSAGE_SIZE_BYTES = 4096;

    // Maximum time the leader waits for every peer to become ready.
    static constexpr std::size_t DEFAULT_READY_TIMEOUT_MS = 30000;

    // Maximum time the leader waits to send one lifecycle control message.
    static constexpr std::size_t DEFAULT_CONTROL_SEND_TIMEOUT_MS = 5000;

    // Maximum time a peer waits to place one algorithm message in a full queue.
    static constexpr std::size_t DEFAULT_DATA_SEND_TIMEOUT_MS = 10;

    std::size_t controlQueueCapacity{0};
    std::size_t dataQueueCapacity{DEFAULT_DATA_QUEUE_CAPACITY};
    std::size_t maxMessageSizeBytes{DEFAULT_MAX_MESSAGE_SIZE_BYTES};
    std::size_t readyTimeoutMs{DEFAULT_READY_TIMEOUT_MS};
    std::size_t controlSendTimeoutMs{DEFAULT_CONTROL_SEND_TIMEOUT_MS};
    std::size_t dataSendTimeoutMs{DEFAULT_DATA_SEND_TIMEOUT_MS};
};

BoostMqQueueConfig parseBoostMqConfig(const nlohmann::json& experiment, int initialPeers);
void preflightBoostMqQueues(const BoostMqQueueConfig& queueConfig, std::size_t experimentIndex);
void validateBoostMqAssignmentPayloads(const std::vector<PeerAssignment>& assignments,
                                       const BoostMqQueueConfig& queueConfig,
                                       std::size_t experimentIndex);

} // namespace quantas

#endif
