#ifndef PROCESS_COORDINATOR_MQ_HPP
#define PROCESS_COORDINATOR_MQ_HPP

#include "../NetworkInterface.hpp" // IWYU pragma: keep
#include <atomic>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <cstddef>
#include <optional>
#include <string>

#define MAX_MSG_SIZE 1024

namespace quantas {
enum class StopMode { FixedRounds, DoneSignals };

class ProcessCoordinatorMQ {
  private:
    /* -------------------- Experiment/process identity context --------------------
    Set once per experiment via configureExperiment(...). This identifies role,
    peer count, and local identity for all subsequent coordinator actions */
    bool _isLeader{false};
    size_t _totalPeers{0};
    interfaceId _myId{NO_PEER_ID};
    size_t _experimentIndex{0};
    std::string _peerType;
    bool _configured{false};
    std::atomic<bool> _stopSignal{false};

    /* -------------------- Experiment policy/config metadata --------------------
    Used to keep runtime behavior tied to experiment-level configuration */
    std::string _logFileBase;
    StopMode _stopMode;

    /* -------------------- Rendezvous transport handles --------------------
    _myBarrier: leader-side barrier queue used for ready fan-in.
    _myInbox: per-peer inbox used for start (and later control) messages */
    std::optional<boost::interprocess::message_queue> _myBarrier;
    std::optional<boost::interprocess::message_queue> _myInbox;

    // -------------------- Lifetime/singleton control --------------------
    ProcessCoordinatorMQ() = default;
    ~ProcessCoordinatorMQ();
    ProcessCoordinatorMQ(const ProcessCoordinatorMQ &) = delete;
    ProcessCoordinatorMQ &operator=(const ProcessCoordinatorMQ &) = delete;

  public:
    // -------------------- Singleton access --------------------
    static ProcessCoordinatorMQ &instance();

    /* -------------------- Configuration API --------------------
    Primary entry point used by MQ runtimes to bind this coordinator to one
    experiment's role and stop policy */
    void configureExperiment(
        size_t experimentIndex, const std::string &peerType, bool isLeader, size_t totalPeers,
        interfaceId myId, const std::string &logFileBase, StopMode stopMode
    );
    // Legacy wrapper kept for compatibility with older call sites.
    void configureProcess(bool isLeader, size_t totalPeers, interfaceId myId);

    // -------------------- start-gate handshake protocol --------------------
    void createBarrier();
    void createInbox();
    void sendReady();
    void waitForAllReady();
    void broadcastStart();
    void waitForStart();

    /* -------------------- Cleanup/lifecycle helpers --------------------
    Remove MQ queues created for this experiment/process */
    void cleanUp();

    /* -------------------- Stop policy query --------------------
    Placeholder for J9/J12 termination semantics (currently minimal behavior) */
    bool shouldStop() const;
    StopMode stopMode() const;
    void requestStop(const std::string &reason = "");
    void broadcastStop();
    void waitForStop();
    void notifyPeerStopped(interfaceId id);
    void waitForAllDone();
};

} // namespace quantas

#endif
