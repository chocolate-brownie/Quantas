#include "quantas/Common/NetworkInterface.hpp"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc != 2)
        return 1;

    const int peerCount = std::atoi(argv[1]);
    boost::interprocess::message_queue barrier(boost::interprocess::open_only, "mq_barrier");
    for (quantas::interfaceId peerId = 0; peerId < peerCount; ++peerId)
        barrier.send(&peerId, sizeof(peerId), 0);
    return 0;
}
