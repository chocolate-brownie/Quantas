#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>

namespace {

bool throwsWithText(const std::function<void()>& operation, const std::string& expectedText) {
    try {
        operation();
    } catch (const std::runtime_error& ex) {
        return std::string(ex.what()).find(expectedText) != std::string::npos;
    }
    return false;
}

} // namespace

int main() {
    const nlohmann::json experimentWithoutBoostMq = nlohmann::json::object();
    const quantas::BoostMqQueueConfig defaults =
        quantas::parseBoostMqConfig(experimentWithoutBoostMq, 3);

    assert(defaults.controlQueueCapacity == 3);
    assert(defaults.dataQueueCapacity == quantas::BoostMqQueueConfig::DEFAULT_DATA_QUEUE_CAPACITY);
    assert(defaults.maxMessageSizeBytes ==
           quantas::BoostMqQueueConfig::DEFAULT_MAX_MESSAGE_SIZE_BYTES);
    assert(defaults.readyTimeoutMs == quantas::BoostMqQueueConfig::DEFAULT_READY_TIMEOUT_MS);
    assert(defaults.controlSendTimeoutMs ==
           quantas::BoostMqQueueConfig::DEFAULT_CONTROL_SEND_TIMEOUT_MS);

    const nlohmann::json validExperiment = nlohmann::json::parse(R"({
        "boostMq": {
            "dataQueueCapacity": 25,
            "maxMessageSizeBytes": 512,
            "readyTimeoutMs": 250,
            "controlSendTimeoutMs": 75
        }
    })");
    const quantas::BoostMqQueueConfig validConfig = quantas::parseBoostMqConfig(validExperiment, 4);

    assert(validConfig.controlQueueCapacity == 4);
    assert(validConfig.dataQueueCapacity == 25);
    assert(validConfig.maxMessageSizeBytes == 512);
    assert(validConfig.readyTimeoutMs == 250);
    assert(validConfig.controlSendTimeoutMs == 75);
    quantas::preflightBoostMqQueues(validConfig, 0);

    quantas::PeerAssignment assignment;
    assignment.id = 0;
    assignment.topologyType = "complete";
    assignment.neighbors = {1, 2, 3};
    const std::vector<quantas::PeerAssignment> assignments{assignment};
    quantas::validateBoostMqAssignmentPayloads(assignments, validConfig, 0);

    const nlohmann::json zeroCapacity = nlohmann::json::parse(R"({
        "boostMq": {"dataQueueCapacity": 0}
    })");
    assert(throwsWithText([&] { quantas::parseBoostMqConfig(zeroCapacity, 2); },
                          "boostMq.dataQueueCapacity"));

    const nlohmann::json negativeCapacity = nlohmann::json::parse(R"({
        "boostMq": {"dataQueueCapacity": -1}
    })");
    assert(throwsWithText([&] { quantas::parseBoostMqConfig(negativeCapacity, 2); },
                          "boostMq.dataQueueCapacity"));

    const nlohmann::json zeroMessageSize = nlohmann::json::parse(R"({
        "boostMq": {"maxMessageSizeBytes": 0}
    })");
    assert(throwsWithText([&] { quantas::parseBoostMqConfig(zeroMessageSize, 2); },
                          "boostMq.maxMessageSizeBytes"));

    for (const nlohmann::json invalidTimeout :
         {nlohmann::json(0), nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json("100")}) {
        const nlohmann::json invalidExperiment = {
            {"boostMq", {{"readyTimeoutMs", invalidTimeout}}}};
        assert(throwsWithText([&] { quantas::parseBoostMqConfig(invalidExperiment, 2); },
                              "boostMq.readyTimeoutMs"));
    }

    for (const nlohmann::json invalidTimeout :
         {nlohmann::json(0), nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json("100")}) {
        const nlohmann::json invalidExperiment = {
            {"boostMq", {{"controlSendTimeoutMs", invalidTimeout}}}};
        assert(throwsWithText([&] { quantas::parseBoostMqConfig(invalidExperiment, 2); },
                              "boostMq.controlSendTimeoutMs"));
    }

    quantas::BoostMqQueueConfig undersizedConfig = validConfig;
    undersizedConfig.maxMessageSizeBytes = 16;
    assert(throwsWithText(
        [&] { quantas::validateBoostMqAssignmentPayloads(assignments, undersizedConfig, 7); },
        "topology assignment for peer 0 requires"));

    return 0;
}
