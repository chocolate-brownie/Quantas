#include "../ProcessCoordinatorMQ.hpp"
#include <chrono>
#include <iostream>
#include <thread>

/* The leader's only job in this test is rendezvous + delay + cleanup. It doesn't send or receive
 * anything itself. */

int main() {
    auto &coord = quantas::ProcessCoordinatorMQ::instance();

    std::cout << "Step 1: configureProcess (leader, 1 peer)" << std::endl;
    coord.configureProcess(true, 2, 0);

    std::cout << "Step 2: createBarrier" << std::endl;
    coord.createBarrier();

    std::cout << "Step 3: waitForAllReady (blocking until follower sends ready)..." << std::endl;
    coord.waitForAllReady();
    std::cout << "  -> all peers ready" << std::endl;

    std::cout << "Step 4: broadcastStart" << std::endl;
    coord.broadcastStart();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Step 5: cleanUp" << std::endl;
    coord.cleanUp();

    std::cout << "Leader done." << std::endl;
    return 0;
}
