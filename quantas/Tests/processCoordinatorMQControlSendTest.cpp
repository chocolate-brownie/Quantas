#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cassert>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>

namespace {
using boost::interprocess::message_queue;
using namespace std::chrono_literals;

constexpr const char* kPeerControlQueue = "peer_0_control";

void removeControlQueue() { message_queue::remove(kPeerControlQueue); }

quantas::ProcessCoordinatorMQ& createLeader(std::size_t timeoutMs) {
    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();
    quantas::BoostMqQueueConfig queueConfig;
    queueConfig.controlQueueCapacity = 1;
    queueConfig.controlSendTimeoutMs = timeoutMs;
    coordinator.configureExperiment(0, "test", true, 1, quantas::NO_PEER_ID, "cout",
                                    quantas::StopMode::FixedRounds, queueConfig);
    return coordinator;
}

std::string thrownMessage(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const std::runtime_error& ex) {
        return ex.what();
    }
    return {};
}

quantas::PeerAssignment assignmentForPeerZero() {
    quantas::PeerAssignment assignment;
    assignment.id = 0;
    assignment.topologyType = "complete";
    return assignment;
}

void testMissingQueueErrorsNameOperationPeerAndTimeout() {
    removeControlQueue();
    auto& coordinator = createLeader(50);
    const std::vector<quantas::PeerAssignment> assignments{assignmentForPeerZero()};

    const std::string assignmentError =
        thrownMessage([&] { coordinator.sendAssignments(assignments); });
    const std::string startError = thrownMessage([&] { coordinator.broadcastStart(); });
    const std::string stopError = thrownMessage([&] { coordinator.broadcastStop(); });

    assert(assignmentError.find("assignment to peer 0 within 50 ms") != std::string::npos);
    assert(startError.find("start to peer 0 within 50 ms") != std::string::npos);
    assert(stopError.find("stop to peer 0 within 50 ms") != std::string::npos);
}

void testFullQueueReturnsAtDeadline() {
    removeControlQueue();
    message_queue queue(boost::interprocess::create_only, kPeerControlQueue, 1,
                        sizeof(unsigned int));
    const unsigned int existingMessage = 1;
    queue.send(&existingMessage, sizeof(existingMessage), 0);

    auto& coordinator = createLeader(50);
    const auto startTime = std::chrono::steady_clock::now();
    const std::string error = thrownMessage([&] { coordinator.broadcastStart(); });
    const auto elapsed = std::chrono::steady_clock::now() - startTime;

    removeControlQueue();
    assert(error.find("Timed out sending start to peer 0 after 50 ms") != std::string::npos);
    assert(elapsed >= 40ms);
    assert(elapsed < 1s);
}
} // namespace

int main() {
    testMissingQueueErrorsNameOperationPeerAndTimeout();
    testFullQueueReturnsAtDeadline();
    removeControlQueue();
    return 0;
}
