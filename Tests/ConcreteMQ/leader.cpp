#include "../../quantas/Common/ConcreteMQ/ProcessCoordinatorMQ.hpp"
#include <iostream>

int main() {
    auto& coord = quantas::ProcessCoordinatorMQ::instance();

    std::cout << "Step 1: configureProcess (leader, 1 peer)" << std::endl;
    coord.configureProcess(true, 1, 0);

    std::cout << "Step 2: createBarrier" << std::endl;
    coord.createBarrier();

    std::cout << "Step 3: waitForAllReady (blocking until follower sends ready)..." << std::endl;
    coord.waitForAllReady();
    std::cout << "  -> all peers ready" << std::endl;

    std::cout << "Step 4: broadcastStart" << std::endl;
    coord.broadcastStart();

    std::cout << "Step 5: cleanUp" << std::endl;
    coord.cleanUp();

    std::cout << "Leader done." << std::endl;
    return 0;
}
