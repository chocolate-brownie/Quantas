#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using boost::interprocess::message_queue;
using namespace std::chrono_literals;

quantas::ProcessCoordinatorMQ& createLeader(size_t peerCount, size_t readyTimeoutMs = 30000) {
    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();
    quantas::BoostMqQueueConfig queueConfig;
    queueConfig.controlQueueCapacity = peerCount;
    queueConfig.readyTimeoutMs = readyTimeoutMs;
    coordinator.configureExperiment(0, "test", true, peerCount, quantas::NO_PEER_ID, "cout",
                                    quantas::StopMode::FixedRounds, queueConfig);
    coordinator.createBarrier();
    return coordinator;
}

void sendReady(quantas::interfaceId peerId) {
    message_queue barrier(boost::interprocess::open_only, "mq_barrier");
    barrier.send(&peerId, sizeof(peerId), 0);
}

bool queueExists(const char* name) {
    try {
        message_queue queue(boost::interprocess::open_only, name);
        return true;
    } catch (const boost::interprocess::interprocess_exception&) {
        return false;
    }
}

void testUniquePeerIdsCompleteBarrier() {
    auto& coordinator = createLeader(3);
    std::vector<quantas::interfaceId> readyPeerIds;
    bool readyTimedOut = false;
    auto result = std::async(std::launch::async,
                             [&] { coordinator.waitForAllReady(readyPeerIds, readyTimedOut); });

    sendReady(0);
    sendReady(0);
    sendReady(1);
    const bool blockedWhilePeerIsMissing = result.wait_for(50ms) == std::future_status::timeout;

    sendReady(2);
    const bool completed = result.wait_for(1s) == std::future_status::ready;
    if (completed)
        result.get();
    coordinator.cleanUp();

    assert(blockedWhilePeerIsMissing);
    assert(completed);
    assert(!readyTimedOut);
    assert((readyPeerIds == std::vector<quantas::interfaceId>{0, 1, 2}));
    assert(!queueExists("mq_barrier"));
    assert(!queueExists("mq_done"));
}

void testInvalidPeerIdFails() {
    auto& coordinator = createLeader(3);
    std::vector<quantas::interfaceId> readyPeerIds;
    bool readyTimedOut = false;
    auto result = std::async(std::launch::async,
                             [&] { coordinator.waitForAllReady(readyPeerIds, readyTimedOut); });

    sendReady(3);
    const bool completed = result.wait_for(1s) == std::future_status::ready;
    std::string error;
    if (completed) {
        try {
            result.get();
        } catch (const std::runtime_error& ex) {
            error = ex.what();
        }
    }
    coordinator.cleanUp();

    assert(completed);
    assert(error.find("Invalid ready peer ID 3") != std::string::npos);
    assert(!queueExists("mq_barrier"));
    assert(!queueExists("mq_done"));
}

void testTimeoutReportsReadyPeers() {
    auto& coordinator = createLeader(3, 50);
    sendReady(1);

    std::vector<quantas::interfaceId> readyPeerIds;
    bool readyTimedOut = false;
    coordinator.waitForAllReady(readyPeerIds, readyTimedOut);
    coordinator.cleanUp();

    assert(readyTimedOut);
    assert((readyPeerIds == std::vector<quantas::interfaceId>{1}));
    assert(!queueExists("mq_barrier"));
    assert(!queueExists("mq_done"));
}
} // namespace

int main() {
    testUniquePeerIdsCompleteBarrier();
    testInvalidPeerIdFails();
    testTimeoutReportsReadyPeers();
    return 0;
}
