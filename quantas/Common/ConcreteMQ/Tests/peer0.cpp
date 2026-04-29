#include "../NetworkInterfaceConcreteMQ.hpp"
#include "../ProcessCoordinatorMQ.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    auto &coord = quantas::ProcessCoordinatorMQ::instance();

    // Randezvous
    std::cout << "[peer 0] Configuring process" << std::endl;
    coord.configureProcess(false, 2, 0);
    std::cout << "[peer 0] Creating inbox process" << std::endl;
    coord.createInbox();
    std::cout << "[peer 0] Sending ready" << std::endl;
    coord.sendReady();
    std::cout << "[peer 0] Waiting for start" << std::endl;
    coord.waitForStart();
    std::cout << "[peer 0] Rendezvous done" << std::endl;

    // Now the message-exchange phase
    quantas::NetworkInterfaceConcreteMQ iface;
    iface.configure(0, {1}); // I am 0 my neighbour is 1

    nlohmann::json msg = {{"Hello, ", "from peer 0"}};
    iface.unicastTo(msg, 1);
    std::cout << "[peer 0] sent a message to peer 1" << std::endl;

    // Give peer 1 time to send to us
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    iface.receive();
    while (!iface.inStreamEmpty()) {
        quantas::Packet p = iface.popInStream();
        std::cout << "[peer 0] received from " << p.sourceId() << ": " << p.getMessage().dump()
                  << std::endl;
    }

    coord.cleanUp();
    return 0;
}
