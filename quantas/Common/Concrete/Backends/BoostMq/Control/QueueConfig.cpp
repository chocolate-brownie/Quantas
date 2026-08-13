#include "QueueConfig.hpp"
#include "quantas/Common/Logger.hpp"
#include <algorithm>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace quantas {

/*
 * Read the BoostMQ queue settings for one experiment.
 *
 * The control queue capacity comes from the experiment's peer count. Data queue
 * capacity and maximum message size come from the optional "boostMq" JSON
 * object, or use their built-in defaults when that object is missing. Invalid
 * values produce an error that identifies the incorrect setting.
 */
BoostMqQueueConfig parseBoostMqQueueConfig(const nlohmann::json& experiment, int initialPeers) {
    BoostMqQueueConfig config;
    config.controlQueueCapacity = initialPeers;

    if (!experiment.contains("boostMq")) return config;

    const auto& boostMq = experiment["boostMq"];

    if (!boostMq.is_object()) throw std::runtime_error("boostMq must be a JSON object");

    if (boostMq.contains("dataQueueCapacity")) {
        const auto& value = boostMq["dataQueueCapacity"];

        if (!value.is_number_unsigned() || value.get<std::size_t>() == 0)
            throw std::runtime_error("boostMq.dataQueueCapacity must be a positive whole number");

        config.dataQueueCapacity = value.get<std::size_t>();
    }

    if (boostMq.contains("maxMessageSizeBytes")) {
        const auto& value = boostMq["maxMessageSizeBytes"];

        if (!value.is_number_unsigned() || value.get<std::size_t>() == 0)
            throw std::runtime_error("boostMq.maxMessageSizeBytes must be a positive whole number");

        config.maxMessageSizeBytes = value.get<std::size_t>();
    }

    return config;
}

/*
 * Check that every serialized topology assignment fits inside a control queue
 * message before any worker process starts.
 *
 * The function serializes each assignment exactly as the leader will send it.
 * If one is too large, it reports the experiment, peer, required bytes, and
 * configured maximum message size.
 */
void validateBoostMqAssignmentPayloads(
    const std::vector<PeerAssignment>& assignments,
    const BoostMqQueueConfig& queueConfig,
    std::size_t experimentIndex
) {
    for (const PeerAssignment& assignment : assignments) {
        std::stringstream stream;
        boost::archive::binary_oarchive archive(stream);
        archive << assignment;

        const std::size_t payloadSize = stream.str().size();
        if (payloadSize <= queueConfig.maxMessageSizeBytes) continue;

        throw std::runtime_error(
            "BoostMQ preflight failed for experiment " + std::to_string(experimentIndex) +
            ": topology assignment for peer " + std::to_string(assignment.id) + " requires " +
            std::to_string(payloadSize) + " bytes, but boostMq.maxMessageSizeBytes is " +
            std::to_string(queueConfig.maxMessageSizeBytes)
        );
    }
}

/*
 * Check that the operating system can create queues using the requested
 * capacities and maximum message size.
 *
 * The function first rejects zero or impossible sizes, then creates temporary
 * control and data queues using the real configuration. Temporary queues are
 * removed after success and also when queue creation fails.
 */
void preflightBoostMqQueues(const BoostMqQueueConfig& queueConfig, std::size_t experimentIndex) {
    if (queueConfig.controlQueueCapacity == 0 || queueConfig.dataQueueCapacity == 0 ||
        queueConfig.maxMessageSizeBytes == 0) {
        throw std::runtime_error(
            "BoostMQ preflight failed for experiment " + std::to_string(experimentIndex) +
            ": queue capacities and maximum message size must be greater than 0"
        );
    }

    const std::size_t largestCapacity = std::max(
        queueConfig.controlQueueCapacity,
        queueConfig.dataQueueCapacity
    );

    if (largestCapacity >
        std::numeric_limits<std::size_t>::max() / queueConfig.maxMessageSizeBytes) {
        throw std::runtime_error(
            "BoostMQ preflight failed for experiment " + std::to_string(experimentIndex) +
            ": requested queue size exceeds the supported size range"
        );
    }

    const std::string queueSuffix = std::to_string(::getpid()) + "_" +
                                    std::to_string(experimentIndex);

    const std::string controlQueueName = "quantas_preflight_control_" + queueSuffix;
    const std::string dataQueueName = "quantas_preflight_data_" + queueSuffix;

    const auto removeTemporaryQueues = [&] {
        boost::interprocess::message_queue::remove(controlQueueName.c_str());
        boost::interprocess::message_queue::remove(dataQueueName.c_str());
    };

    removeTemporaryQueues();

    try {
        boost::interprocess::message_queue temporaryControlQueue(
            boost::interprocess::create_only,
            controlQueueName.c_str(),
            queueConfig.controlQueueCapacity,
            queueConfig.maxMessageSizeBytes
        );
        QUANTAS_LOG_INFO("preflight") << "temporary control queue created successfully";

        boost::interprocess::message_queue temporaryDataQueue(
            boost::interprocess::create_only,
            dataQueueName.c_str(),
            queueConfig.dataQueueCapacity,
            queueConfig.maxMessageSizeBytes
        );
        QUANTAS_LOG_INFO("preflight") << "temporary data queue created successfully";
    } catch (const boost::interprocess::interprocess_exception& ex) {
        removeTemporaryQueues();
        throw std::runtime_error(
            "BoostMQ preflight failed for experiment " + std::to_string(experimentIndex) + ": " +
            ex.what()
        );
    }

    removeTemporaryQueues();
}

} // namespace quantas
