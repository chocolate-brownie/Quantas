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
#include <string>

#define MAX_MSG_SIZE 1024

namespace quantas {
enum class StopMode { FixedRounds, DoneSignals };

class ProcessCoordinatorMQ {
  private:
    bool _isLeader{false};
    size_t _totalPeers{0};
    interfaceId _myId{NO_PEER_ID};
    size_t _experimentIndex{0};
    std::string _peerType;
    bool _configured{false};
    std::string _logFileBase;
    StopMode _stopMode;

    std::optional<boost::interprocess::message_queue> _myBarrier;
    std::optional<boost::interprocess::message_queue> _myInbox;

    ProcessCoordinatorMQ() = default;
    ~ProcessCoordinatorMQ();
    ProcessCoordinatorMQ(const ProcessCoordinatorMQ &) = delete;
    ProcessCoordinatorMQ &operator=(const ProcessCoordinatorMQ &) = delete;

  public:
    static ProcessCoordinatorMQ &instance();
    // Experiment-scoped configuration entry point (J2 skeleton).
    void configureExperiment(
        size_t experimentIndex, const std::string &peerType, bool isLeader, size_t totalPeers,
        interfaceId myId, const std::string &logFileBase, StopMode stopMode
    );
    // Backward-compatible wrapper used by current call sites.
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
