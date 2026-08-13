#ifndef QUANTAS_COMMON_RUNTIME_METRICS_TRANSPORTMETRICS_HPP
#define QUANTAS_COMMON_RUNTIME_METRICS_TRANSPORTMETRICS_HPP

#include "quantas/Common/Json.hpp"
#include <cstddef>
#include <cstdint>

namespace quantas {

struct TransportMetrics {
    // Peer successfully placed N messages into other peers’ queues.
    uint64_t sent = 0;

    // Peer pulled N raw messages from its own BoostMQ queue.
    uint64_t receivedRaw = 0;

    /* Every raw message it pulled was successfully decoded and passed into Quantas’ local input
     * stream.*/
    uint64_t deliveredToInstream = 0;

    /* This peer also tried to send N additional messages, but destination queues were full or
    not accepting fast enough, so those messages were dropped.*/
    uint64_t droppedBackpressure = 0;

    // Largest number of messages waiting in the queue at one observed moment
    uint64_t peakQueueUsage{0};

    /*
    # Best Health Check

    ### For the whole run:
    transport_valid = dropped_backpressure_total == 0 && received_raw_total ==
    delivered_to_instream_total

    ### Stronger version:
    transport_valid = (dropped_backpressure_total == 0 && sent_total == received_raw_total &&
    received_raw_total == delivered_to_instream_total

    The stronger version may fail if shutdown leaves
    messages in queues, so use it carefully. But dropped_backpressure == 0 is non-negotiable for
    “reliable delivery accepted by transport.” */
};

inline nlohmann::json
makeTransportMetricsJson(const TransportMetrics& metrics, std::size_t dataQueueCapacity) {
    return {
        // Messages successfully added to other peers' queues.
        {"sent", metrics.sent},

        // Messages removed from this peer's queue.
        {"received_raw", metrics.receivedRaw},

        // Received messages successfully passed to the peer.
        {"delivered_to_instream", metrics.deliveredToInstream},

        // Messages dropped because a destination queue stayed full.
        {"dropped_backpressure", metrics.droppedBackpressure},

        // Maximum number of messages this peer's queue can hold.
        {"data_queue_capacity", dataQueueCapacity},

        // Largest number of waiting messages observed in this peer's queue.
        {"peak_observed_queue_usage", metrics.peakQueueUsage},
    };
}

} // namespace quantas

#endif
