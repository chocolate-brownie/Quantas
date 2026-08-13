/* @brief
`mq_timeout_test` is a small test target in the `makefile` that checks the BoostMQ leader’s
**done-wait timeout behavior**.

It builds/runs this file:

```text
quantas/Tests/processCoordinatorMQTimeoutTest.cpp
```

The test exercises `ProcessCoordinatorMQ::waitForAllDone(...)`.

Conceptually, it creates the leader-side control queues, sends a few fake `done` messages, and
verifies that the leader behaves correctly when not all peers finish before the timeout.

It checks cases like:

```text
peer 0 sends done
peer 0 sends duplicate done
invalid peer id 99 sends done
peer 1 sends done
peer 2 never sends done
leader times out waiting for peer 2
```
*/

#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cassert>
#include <chrono>
#include <thread>

int main() {
    using boost::interprocess::message_queue;
    using namespace std::chrono_literals;

    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();
    quantas::BoostMqQueueConfig queueConfig;
    queueConfig.controlQueueCapacity = 3;
    coordinator.configureExperiment(
        0,
        "test",
        true,
        3,
        quantas::NO_PEER_ID,
        "cout",
        quantas::StopMode::FixedRounds,
        queueConfig
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
