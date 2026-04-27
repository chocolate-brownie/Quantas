#include "../../quantas/Common/ConcreteMQ/ProcessCoordinatorMQ.hpp"
#include <iostream>

int main() {
    auto& coord = quantas::ProcessCoordinatorMQ::instance();

    std::cout << "Step 1: configureProcess (follower, id=1)" << std::endl;
    coord.configureProcess(false, 1, 0);

    std::cout << "Step 2: createInbox" << std::endl;
    coord.createInbox();

    std::cout << "Step 3: sendReady" << std::endl;
    coord.sendReady();

    std::cout << "Step 4: waitForStart (blocking until leader sends start)..." << std::endl;
    coord.waitForStart();
    std::cout << "  -> start received" << std::endl;

    std::cout << "Step 5: cleanUp" << std::endl;
    coord.cleanUp();

    std::cout << "Follower done." << std::endl;
    return 0;
}
