#ifndef PROCESS_COORDINATOR_MQ_HPP
#define PROCESS_COORDINATOR_MQ_HPP

/* Steps ...
1. Leader creates quantas_barrier queue first
2. Every follower creates their own inbox
3. Every follower sends "ready" into quantas_barrier
4. Leader reads N "ready" messages from quantas_barrier
5. Leader sends "start" into each peer's inbox
6. Every follower reads "start" from their inbox → begin */

#include "../NetworkInterface.hpp" // IWYU pragma: keep
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cstddef>
#include <optional>

#define MAX_MSG_SIZE 1024

namespace quantas {

class ProcessCoordinatorMQ {
  private:
    bool _isLeader{false};
    size_t _totalPeers{0};
    interfaceId _myId{NO_PEER_ID};

    std::optional<boost::interprocess::message_queue> _myBarrier;
    std::optional<boost::interprocess::message_queue> _myInbox;

    ProcessCoordinatorMQ() = default;
    ~ProcessCoordinatorMQ();
    ProcessCoordinatorMQ(const ProcessCoordinatorMQ &) = delete;
    ProcessCoordinatorMQ &operator=(const ProcessCoordinatorMQ &) = delete;

  public:
    static ProcessCoordinatorMQ &instance();
    void configureProcess(bool isLeader, size_t totalPeers, interfaceId myId);

    void createBarrier();
    void createInbox();
    void sendReady();
    void waitForAllReady();
    void broadcastStart();
    void waitForStart();
    void cleanUp();
};

} // namespace quantas

#endif
