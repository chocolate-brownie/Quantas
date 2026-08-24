#ifndef PROCESS_COORDINATOR_MQ_HPP
#define PROCESS_COORDINATOR_MQ_HPP

#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/PeerAssignment.hpp"
#include "quantas/Common/NetworkInterface.hpp" // IWYU pragma: keep
#include <atomic>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace quantas {
enum class StopMode { FixedRounds, DoneSignals };

struct PeerCompletionMessage {
    interfaceId peerId{NO_PEER_ID};
    bool succeeded{true};
};

struct PeerCompletionResult {
    std::vector<interfaceId> completedPeers;
    std::vector<interfaceId> failedPeers;
    bool timedOut{false};
};

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

    /* -------------------- Experiment boostmq queue/config metadata --------------------*/
    BoostMqQueueConfig _queueConfig;

    /* -------------------- Rendezvous transport handles --------------------
    _myBarrier: leader-side barrier queue used for ready fan-in.
    _myControlInbox: per-peer inbox used for assignment/start/stop control messages */
    std::optional<boost::interprocess::message_queue> _myBarrier;
    std::optional<boost::interprocess::message_queue> _myControlInbox;

    // -------------------- Lifetime/singleton control --------------------
    ProcessCoordinatorMQ() = default;
    ~ProcessCoordinatorMQ();
    ProcessCoordinatorMQ(const ProcessCoordinatorMQ&) = delete;
    ProcessCoordinatorMQ& operator=(const ProcessCoordinatorMQ&) = delete;
    void notifyPeerFinished(interfaceId id, bool succeeded);

  public:
    // -------------------- Singleton access --------------------
    static ProcessCoordinatorMQ& instance();

    /* -------------------- Configuration API --------------------
    Primary entry point used by MQ runtimes to bind this coordinator to one
    experiment's role and stop policy */
    void configureExperiment(size_t experimentIndex, const std::string& peerType, bool isLeader,
                             size_t totalPeers, interfaceId myId, const std::string& logFileBase,
                             StopMode stopMode, const BoostMqQueueConfig& queueConfig);
    // Legacy wrapper kept for compatibility with older call sites.
    void configureProcess(bool isLeader, size_t totalPeers, interfaceId myId);

    // -------------------- start-gate handshake protocol --------------------
    void createBarrier();
    void createInbox();
    void sendReady();
    void waitForAllReady(std::vector<interfaceId>& readyPeerIds, bool& readyTimedOut);
    void broadcastStart();
    void waitForStart();

    // -------------------- assignment protocol scaffolding --------------------
    void sendAssignments(const std::vector<PeerAssignment>& assignments);
    std::vector<PeerAssignment> waitForAssignments();

    /* -------------------- Cleanup/lifecycle helpers --------------------
    Remove MQ queues created for this experiment/process */
    void cleanUp();

    /* -------------------- Stop policy query --------------------
    Placeholder for J9/J12 termination semantics (currently minimal behavior) */
    bool shouldStop() const;
    StopMode stopMode() const;
    void requestStop(const std::string& reason = "");
    void broadcastStop();
    void broadcastStopBestEffort();
    void waitForStop();
    void notifyPeerStopped(interfaceId id);
    void notifyPeerFailed(interfaceId id);
    PeerCompletionResult waitForAllDone(std::chrono::milliseconds timeout);
};

} // namespace quantas

#endif
