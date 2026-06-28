#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cassert>
#include <chrono>
#include <thread>

int main() {
    using boost::interprocess::message_queue;
    using namespace std::chrono_literals;

    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();
    coordinator.configureExperiment(
        0, "test", true, 3, quantas::NO_PEER_ID, "cout", quantas::StopMode::FixedRounds
    );
    coordinator.createBarrier();

    std::thread sender([] {
        std::this_thread::sleep_for(10ms);
        message_queue doneQueue(boost::interprocess::open_only, "mq_done");
        for (quantas::interfaceId id : {0, 0, 99, 1}) { doneQueue.send(&id, sizeof(id), 0); }
    });

    const auto start = std::chrono::steady_clock::now();
    const quantas::PeerCompletionResult result = coordinator.waitForAllDone(100ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    sender.join();
    coordinator.cleanUp();

    assert(result.timedOut);
    assert((result.completedPeers == std::vector<quantas::interfaceId>{0, 1}));
    assert(elapsed >= 90ms);
    assert(elapsed < 1s);
    return 0;
}
